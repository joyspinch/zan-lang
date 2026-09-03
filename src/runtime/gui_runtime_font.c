/* gui_runtime_font.c -- the software bitmap-font fallback plus EWMH/Xlib window management
 * and client-side title-bar metrics for non-Windows backends.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * Software bitmap-font fallback for non-Windows/non-Cocoa backends.
 * ======================================================================== */
#if !defined(_WIN32) && !defined(ZAN_GUI_COCOA)
/* Fallback bitmap font for software text rendering */
static const unsigned char zan_font_6x10[96][10] = {
    /* space (32) */ {0},
    /* ! */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00,0x00,0x00},
    /* " */ {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* # */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0x00,0x00,0x00},
    /* $ */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x00,0x00,0x00},
    /* % */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00,0x00,0x00},
    /* & */ {0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0x00,0x00,0x00},
    /* ' */ {0x04,0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* ( */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00,0x00,0x00},
    /* ) */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00,0x00,0x00},
    /* * */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00,0x00,0x00},
    /* + */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00,0x00,0x00},
    /* , */ {0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x08,0x00,0x00},
    /* - */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00},
    /* . */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00},
    /* / */ {0x01,0x02,0x02,0x04,0x08,0x08,0x10,0x00,0x00,0x00},
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00,0x00,0x00},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00,0x00,0x00},
    /* 3 */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,0x00,0x00,0x00},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00,0x00,0x00},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00,0x00,0x00},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00,0x00,0x00},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00,0x00,0x00},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00,0x00,0x00},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00,0x00,0x00},
    /* : */ {0x00,0x00,0x04,0x00,0x00,0x04,0x00,0x00,0x00,0x00},
    /* ; */ {0x00,0x00,0x04,0x00,0x00,0x04,0x04,0x08,0x00,0x00},
    /* < */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00,0x00,0x00},
    /* = */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00,0x00,0x00},
    /* > */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00,0x00,0x00},
    /* ? */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00,0x00,0x00},
    /* @ */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E,0x00,0x00,0x00},
    /* A-Z */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00,0x00,0x00},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00,0x00,0x00},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00,0x00,0x00},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00,0x00,0x00},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00,0x00,0x00},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00,0x00,0x00},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E,0x00,0x00,0x00},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00,0x00,0x00},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00,0x00,0x00},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00,0x00,0x00},
    /* [, \, ], ^, _, ` */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00,0x00,0x00},
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01,0x00,0x00,0x00},
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00,0x00,0x00},
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* a-z */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00,0x00,0x00},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E,0x00,0x00,0x00},
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00,0x00,0x00},
    {0x06,0x08,0x1E,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E,0x00,0x00},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x02,0x00,0x06,0x02,0x02,0x02,0x12,0x0C,0x00,0x00},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12,0x00,0x00,0x00},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10,0x00,0x00},
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01,0x00,0x00},
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E,0x00,0x00,0x00},
    {0x08,0x08,0x1E,0x08,0x08,0x08,0x06,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F,0x00,0x00,0x00},
    /* {, |, }, ~ */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00,0x00,0x00},
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00,0x00,0x00},
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00,0x00,0x00,0x00},
};

static u32 utf8_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    if (!*p) return 0;
    if (*p < 0x80) {
        *text += 1;
        return *p;
    }
    if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x1F) << 6) | (u32)(p[1] & 0x3F);
        *text += 2;
        return cp >= 0x80 ? cp : 0xFFFD;
    }
    if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x0F) << 12) |
                 ((u32)(p[1] & 0x3F) << 6) | (u32)(p[2] & 0x3F);
        *text += 3;
        return cp >= 0x800 ? cp : 0xFFFD;
    }
    if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x07) << 18) |
                 ((u32)(p[1] & 0x3F) << 12) |
                 ((u32)(p[2] & 0x3F) << 6) | (u32)(p[3] & 0x3F);
        *text += 4;
        return cp >= 0x10000 && cp <= 0x10FFFF ? cp : 0xFFFD;
    }
    *text += 1;
    return 0xFFFD;
}

static void bitmap_draw_text(i64 surface_id, i64 x, i64 y,
                             const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    u32 c = (u32)color;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int cx = (int)x;
    int cy_base = (int)y;
    while (*text) {
        u32 cp = utf8_next(&text);
        unsigned char ch = (cp >= 32 && cp <= 127) ? (unsigned char)cp : '?';
        int idx = ch - 32;
        for (int row = 0; row < 10; row++) {
            unsigned char bits = zan_font_6x10[idx][row];
            for (int col = 0; col < 6; col++) {
                if (bits & (0x10 >> col)) {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            set_pixel(s, cx + col*scale + sx, cy_base + row*scale + sy, c);
                }
            }
        }
        cx += 6 * scale;
    }
}

static i64 bitmap_measure_text(const char *text, i64 font_size) {
    if (!text) return 0;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int len = 0;
    while (*text) { utf8_next(&text); len++; }
    return (i64)(len * 6 * scale);
}

static i64 bitmap_font_height(i64 font_size) {
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    return (i64)(10 * scale);
}

#ifdef ZAN_GUI_FREETYPE
static FT_Library g_ft_library;
static FT_Face g_ft_face;
static int g_ft_state;

#if defined(__ANDROID__)
/* The device's default and CJK faces as configured by the ROM. Vendors
 * (MIUI, HarmonyOS, ...) ship their own default font and keep
 * /system/etc/fonts.xml pointing at it, so a fixed "Roboto" path renders
 * every ROM in Noto instead of the font the user actually sees elsewhere.
 * AOSP marks the file deprecated in favour of AFontMatcher (API 29+), but
 * still requires vendors to maintain it; when it disappears or stops
 * parsing, the hardcoded chain in ft_prepare is the fallback. */
static char g_android_def_path[256];
static int g_android_def_idx;
static char g_android_cjk_path[256];
static int g_android_cjk_idx;
static int g_android_cjk_ok;

/* Pull one attribute value ("index", "weight", "lang") out of a tag body
 * [attrs, attrs+attrlen). Returns 0 when absent. */
static int ft_xml_attr(const char *attrs, int attrlen, const char *name,
                       char *out, int outsz) {
    char pat[64];
    int n = snprintf(pat, sizeof(pat), "%s=\"", name);
    const char *p = attrs;
    const char *end = attrs + attrlen;
    while (p + n < end) {
        const char *hit = memchr(p, name[0], (size_t)(end - p));
        if (!hit || hit + n >= end) return 0;
        if (strncmp(hit, pat, (size_t)n) == 0) {
            const char *v = hit + n;
            const char *close = memchr(v, '"', (size_t)(end - v));
            if (!close) return 0;
            int len = (int)(close - v);
            if (len >= outsz) len = outsz - 1;
            memcpy(out, v, (size_t)len);
            out[len] = 0;
            return 1;
        }
        p = hit + 1;
    }
    return 0;
}

static int ft_fonts_xml_scan(void) {
    FILE *f = fopen("/system/etc/fonts.xml", "rb");
    if (!f) return 0;
    static char buf[1 << 21]; /* 2 MiB: the file is ~80 KB today */
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;

    const char *p = buf;
    while ((p = strstr(p, "<family")) != NULL) {
        p += 7;
        /* <familyset and friends are not family blocks */
        if (*p != ' ' && *p != '>') continue;
        const char *gt = strchr(p, '>');
        if (!gt) break;
        int attrlen = (int)(gt - p);
        const char *body = gt + 1;
        const char *fend = strstr(body, "</family>");
        if (!fend) break;

        char attrs[512];
        if (attrlen >= (int)sizeof(attrs)) attrlen = (int)sizeof(attrs) - 1;
        memcpy(attrs, p, (size_t)attrlen);
        attrs[attrlen] = 0;

        char lang[32] = "";
        ft_xml_attr(attrs, attrlen, "lang", lang, sizeof(lang));

        /* Within the family pick the regular upright face: weights are
         * separate entries and often separate *files* (MiSans-Light vs
         * MiSans-Regular), so "first" would grab whatever weight the
         * ROM listed first. */
        char best[256] = "";
        int best_idx = 0, best_score = 1 << 30;
        const char *q = body;
        while (q < fend && (q = strstr(q, "<font")) != NULL && q < fend) {
            q += 5;
            if (*q != ' ' && *q != '>') continue;
            const char *fgt = strchr(q, '>');
            if (!fgt || fgt > fend) break;
            int fattrlen = (int)(fgt - q);
            char fattrs[512];
            if (fattrlen >= (int)sizeof(fattrs)) fattrlen = (int)sizeof(fattrs) - 1;
            memcpy(fattrs, q, (size_t)fattrlen);
            fattrs[fattrlen] = 0;

            const char *txt = fgt + 1;
            const char *ftxt_end = strstr(txt, "</font>");
            if (!ftxt_end || ftxt_end > fend) break;
            /* the text run may contain nested <axis .../> elements: the
             * file name is the leading trimmed text before '<' */
            const char *lt = memchr(txt, '<', (size_t)(ftxt_end - txt));
            if (!lt || lt > ftxt_end) lt = ftxt_end;
            char path[256];
            int ti = 0;
            const char *s = txt;
            while (s < lt && (*s == ' ' || *s == '\n' || *s == '\r' ||
                              *s == '\t')) s++;
            const char *e = lt;
            while (e > s && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r' ||
                             e[-1] == '\t')) e--;
            while (s < e && ti < (int)sizeof(path) - 1) path[ti++] = *s++;
            path[ti] = 0;

            if (ti > 0) {
                char w[16] = "", st[16] = "", ix[16] = "";
                int score = 0;
                if (ft_xml_attr(fattrs, fattrlen, "weight", w, sizeof(w)))
                    score += abs(atoi(w) - 400);
                if (ft_xml_attr(fattrs, fattrlen, "style", st, sizeof(st)) &&
                    strcmp(st, "italic") == 0)
                    score += 10000;
                int idx = 0;
                if (ft_xml_attr(fattrs, fattrlen, "index", ix, sizeof(ix)))
                    idx = atoi(ix);
                if (score < best_score) {
                    best_score = score;
                    snprintf(best, sizeof(best), "/system/fonts/%s", path);
                    best_idx = idx;
                }
            }
            q = ftxt_end + 7;
        }

        if (best[0]) {
            if (!g_android_def_path[0] && !lang[0]) {
                snprintf(g_android_def_path, sizeof(g_android_def_path),
                         "%s", best);
                g_android_def_idx = best_idx;
            }
            if (!g_android_cjk_path[0] && lang[0] == 'z' && lang[1] == 'h') {
                snprintf(g_android_cjk_path, sizeof(g_android_cjk_path),
                         "%s", best);
                g_android_cjk_idx = best_idx;
                g_android_cjk_ok = 1;
            }
        }
        p = (const char *)fend;
    }
    return g_android_def_path[0] && g_android_cjk_ok;
}

static void ft_android_pick_fonts(void) {
    if (ft_fonts_xml_scan()) return;
    /* No fonts.xml (or nothing usable in it): AOSP stock layout. */
    snprintf(g_android_def_path, sizeof(g_android_def_path),
             "/system/fonts/Roboto-Regular.ttf");
    g_android_def_idx = 0;
    snprintf(g_android_cjk_path, sizeof(g_android_cjk_path),
             "/system/fonts/NotoSansCJK-Regular.ttc");
    g_android_cjk_idx = 2;
    g_android_cjk_ok = access(g_android_cjk_path, R_OK) == 0;
}
#endif

static int ft_prepare(int font_size) {
    if (g_ft_state == 0) {
        g_ft_state = -1;
        if (FT_Init_FreeType(&g_ft_library) != 0) return 0;
#if defined(__ANDROID__)
        /* Android ships no fontconfig; the system faces live in
         * /system/fonts. Primary face follows the ROM's own default
         * (fonts.xml first nameless family -- MiSans on MIUI, ...),
         * falling back to AOSP's Roboto; CJK glyphs resolve through
         * ft_face_for_cp's zh family below. */
        ft_android_pick_fonts();
        if (access(g_android_def_path, R_OK) == 0 &&
            FT_New_Face(g_ft_library, g_android_def_path,
                        (FT_Long)g_android_def_idx, &g_ft_face) == 0) {
            g_ft_state = 1;
        } else {
            static const char *const prim_paths[] = {
                "/system/fonts/DroidSans.ttf",
                "/system/fonts/NotoSansCJK-Regular.ttc",
            };
            static const int prim_idx[] = { 0, 2 };
            for (int i = 0; i < 2; i++) {
                if (access(prim_paths[i], R_OK) != 0) continue;
                if (FT_New_Face(g_ft_library, prim_paths[i], prim_idx[i],
                                &g_ft_face) == 0) {
                    g_ft_state = 1;
                    break;
                }
            }
        }
#else
        if (!FcInit()) return 0;
        FcPattern *pattern = FcNameParse((const FcChar8 *)"sans");
        if (!pattern) return 0;
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern *match = FcFontMatch(NULL, pattern, &result);
        FcPatternDestroy(pattern);
        if (!match) return 0;
        FcChar8 *path = NULL;
        int found = FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch;
        if (!found || FT_New_Face(g_ft_library, (const char *)path, 0,
                                  &g_ft_face) != 0) {
            FcPatternDestroy(match);
            return 0;
        }
        FcPatternDestroy(match);
        g_ft_state = 1;
#endif
    }
    if (g_ft_state != 1) return 0;
    if (font_size < 8) font_size = 8;
    return FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)font_size) == 0;
}

#define ZAN_FT_FB_MAX 8
static FT_Face g_ft_fb[ZAN_FT_FB_MAX];
static int g_ft_fb_count = 0;

/* Return a face that can render `cp` at `font_size`, discovering a fallback
 * font when the primary face lacks the glyph (e.g. CJK on a Latin face).
 * Discovered faces are cached; falls back to the primary face when no better
 * match exists. */
static FT_Face ft_face_for_cp(u32 cp, int font_size) {
    if (FT_Get_Char_Index(g_ft_face, cp)) return g_ft_face;
    for (int i = 0; i < g_ft_fb_count; i++) {
        if (g_ft_fb[i] && FT_Get_Char_Index(g_ft_fb[i], cp)) {
            FT_Set_Pixel_Sizes(g_ft_fb[i], 0, (FT_UInt)font_size);
            return g_ft_fb[i];
        }
    }
    if (g_ft_fb_count < ZAN_FT_FB_MAX) {
#if defined(__ANDROID__)
        /* The fallback chain mirrors fonts.xml: the zh family found at
         * init (Noto CJK ttc face 2 on AOSP, the ROM's CJK face on
         * vendor builds), then the serif ttc and DroidSansFallback for
         * anything left. */
        const char *fb_paths[3];
        int fb_idx[3];
        int nfb = 0;
        if (g_android_cjk_ok) {
            fb_paths[nfb] = g_android_cjk_path;
            fb_idx[nfb] = g_android_cjk_idx;
            nfb++;
        }
        fb_paths[nfb] = "/system/fonts/NotoSerifCJK-Regular.ttc";
        fb_idx[nfb] = 0;
        nfb++;
        fb_paths[nfb] = "/system/fonts/DroidSansFallback.ttf";
        fb_idx[nfb] = 0;
        nfb++;
        for (int i = 0; i < nfb; i++) {
            FT_Face face = NULL;
            if (access(fb_paths[i], R_OK) == 0 &&
                FT_New_Face(g_ft_library, fb_paths[i], fb_idx[i],
                            &face) == 0 &&
                FT_Get_Char_Index(face, cp)) {
                g_ft_fb[g_ft_fb_count++] = face;
                FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                return face;
            }
            if (face) FT_Done_Face(face);
        }
#else
        FcCharSet *charset = FcCharSetCreate();
        FcCharSetAddChar(charset, cp);
        FcPattern *pat = FcPatternCreate();
        FcPatternAddCharSet(pat, FC_CHARSET, charset);
        FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        FcPattern *match = FcFontMatch(NULL, pat, &res);
        FcPatternDestroy(pat);
        FcCharSetDestroy(charset);
        if (match) {
            FcChar8 *path = NULL;
            FT_Face face = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch &&
                FT_New_Face(g_ft_library, (const char *)path, 0, &face) == 0) {
                g_ft_fb[g_ft_fb_count++] = face;
                FcPatternDestroy(match);
                FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                return face;
            }
            FcPatternDestroy(match);
        }
#endif
    }
    return g_ft_face;
}

/* One glyph's coverage tile, rasterised on its first use at this size and kept
 * in the glyph atlas: FT_LOAD_RENDER is the most expensive thing in a frame of
 * text, and a repaint draws the same characters over and over. Whatever pixel
 * format FreeType produced is normalised to one coverage byte per pixel here,
 * so nothing downstream (nor a GPU backend's R8 atlas) has to know about
 * FT_PIXEL_MODE_*. */
static const zan_glyph_tile *ft_glyph_tile(u32 cp, int font_size, int angle) {
    char key[8];
    key[0] = (char)(cp & 0xFF);
    key[1] = (char)((cp >> 8) & 0xFF);
    key[2] = (char)((cp >> 16) & 0xFF);
    key[3] = (char)((cp >> 24) & 0xFF);
    key[4] = (char)(angle & 0xFF);
    key[5] = (char)((angle >> 8) & 0xFF);
    const zan_glyph_tile *cached =
        zan_atlas_find(ZAN_TILE_GLYPH, font_size, key, 6);
    if (cached) return cached;

    FT_Face face = ft_face_for_cp(cp, font_size);
    FT_UInt glyph = FT_Get_Char_Index(face, cp);
    if (!glyph) glyph = FT_Get_Char_Index(face, '?');
    /* Rotation is baked into the coverage tile via the face transform, so
     * nothing downstream knows tiles come in orientations: the atlas keeps
     * one tile per (glyph, angle) and the pen loop walks the rotated
     * baseline. The transform rotates slot->advance too, so the unrotated
     * pen advance comes from metrics (26.6, never transformed). FT's font
     * space is y-up and the device is y-down; mapping the device-space
     * clockwise rotation through that flip gives this matrix. */
    int rotated = angle != 0;
    if (rotated) {
        double rad = (double)angle * 3.14159265358979323846 / 180.0;
        FT_Matrix mat;
        mat.xx = (FT_Fixed)(cos(rad) * 65536.0);
        mat.xy = (FT_Fixed)(sin(rad) * 65536.0);
        mat.yx = (FT_Fixed)(-sin(rad) * 65536.0);
        mat.yy = (FT_Fixed)(cos(rad) * 65536.0);
        FT_Set_Transform(face, &mat, NULL);
    }
    int loaded = glyph && FT_Load_Glyph(face, glyph, FT_LOAD_RENDER) == 0;
    int advance = 0;
    if (loaded) {
        advance = (int)((rotated ? face->glyph->metrics.horiAdvance
                                 : face->glyph->advance.x) >> 6);
    }
    if (rotated) { FT_Set_Transform(face, NULL, NULL); }
    if (!loaded) return NULL;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;
    int w = (int)bitmap->width;
    int h = (int)bitmap->rows;
    if (w <= 0 || h <= 0) {
        return zan_atlas_store(ZAN_TILE_GLYPH, font_size, key, 6,
                               0, 0, 0, 0, advance, NULL, 1);
    }

    unsigned char *cov = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!cov) return NULL;
    int pitch = bitmap->pitch;
    for (int py = 0; py < h; py++) {
        const unsigned char *row =
            pitch >= 0 ? bitmap->buffer + py * pitch
                       : bitmap->buffer + (h - 1 - py) * (-pitch);
        unsigned char *dst = cov + (size_t)py * (size_t)w;
        for (int px = 0; px < w; px++) {
            int coverage = 0;
            if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
                coverage = row[px];
            else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
                coverage = (row[px >> 3] & (0x80 >> (px & 7))) ? 255 : 0;
            else if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
                coverage = row[px * 4 + 3];
            dst[px] = (unsigned char)coverage;
        }
    }
    const zan_glyph_tile *tile =
        zan_atlas_store(ZAN_TILE_GLYPH, font_size, key, 6,
                        w, h, slot->bitmap_left, slot->bitmap_top,
                        advance, cov, 1);
    free(cov);
    return tile;
}

static void ft_draw_text(i64 surface_id, i64 x, i64 y,
                         const char *text, i64 color, int font_size,
                         int angle) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    int pen_x = (int)x;
    int baseline = (int)y +
                   (int)(g_ft_face->size->metrics.ascender >> 6);
    zan_glyph_batch batch;
    batch.s = s;
    batch.color = (u32)color;
    batch.count = 0;
    if (angle == 0) {
        while (*text) {
            u32 cp = utf8_next(&text);
            const zan_glyph_tile *tile = ft_glyph_tile(cp, font_size, 0);
            if (!tile) continue;   /* glyph the face could not render: as before */
            zan_glyph_batch_add(&batch, tile, pen_x + tile->left,
                                baseline - tile->top);
            pen_x += tile->advance;
        }
        zan_glyph_batch_flush(&batch);
        return;
    }
    /* Rotated: each glyph's tile already carries the rotation (see
     * ft_glyph_tile), so the run reassembles rigidly by walking the rotated
     * baseline. Unrotated pen points sit at (s, asc) from the anchor with s
     * the accumulated advance; placing rotated glyphs at R(s, asc) through
     * the device-space rotation reproduces exactly that rigid rotation. */
    double rad = (double)angle * 3.14159265358979323846 / 180.0;
    double cs = cos(rad), sn = sin(rad);
    double asc = (double)(g_ft_face->size->metrics.ascender >> 6);
    double s_acc = 0.0;
    while (*text) {
        u32 cp = utf8_next(&text);
        const zan_glyph_tile *tile = ft_glyph_tile(cp, font_size, angle);
        if (!tile) continue;
        double px = cs * s_acc - sn * asc;
        double py = sn * s_acc + cs * asc;
        zan_glyph_batch_add(&batch, tile,
                            (int)((double)x + px + tile->left),
                            (int)((double)y + py - tile->top));
        s_acc += tile->advance;   /* unrotated distance along the baseline */
    }
    zan_glyph_batch_flush(&batch);
}

/* Measured-width cache, ported from gui_runtime_text.c's Win32 one: layout,
 * caret and syntax-highlight paths call measure hundreds of times per frame
 * with unchanged strings, and every miss here costs one FT_Load_Glyph (plus
 * a pixel-size reselect on fallback faces) per code point. The face set is
 * fixed for the life of the process ("sans" plus discovered fallbacks), so
 * (text, size) fully determines the width. */
typedef struct {
    char    *text;   /* UTF-8 key; NULL marks an empty slot */
    int      size;
    int      width;
    uint64_t used;   /* LRU tick */
} ft_measure_cache_t;

#define FT_MEAS_CACHE_CAP 2048
#define FT_MEAS_CACHE_PROBE 8
static ft_measure_cache_t g_ft_mcache[FT_MEAS_CACHE_CAP];
static uint64_t g_ft_mcache_clock = 0;

static uint64_t ft_meas_hash(const char *s, int size) {
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)(unsigned)size;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static i64 ft_measure_text(const char *text, int font_size) {
    if (!text || !*text) return 0;

    uint64_t h = ft_meas_hash(text, font_size);
    int base = (int)(h % FT_MEAS_CACHE_CAP);
    ft_measure_cache_t *victim = NULL;
    uint64_t best = ~0ULL;
    for (int i = 0; i < FT_MEAS_CACHE_PROBE; i++) {
        ft_measure_cache_t *e = &g_ft_mcache[(base + i) % FT_MEAS_CACHE_CAP];
        if (e->text && e->size == font_size && strcmp(e->text, text) == 0) {
            e->used = ++g_ft_mcache_clock;
            return (i64)e->width;
        }
        if (!e->text) { if (!victim || best != 0) { victim = e; best = 0; } }
        else if (e->used < best) { best = e->used; victim = e; }
    }

    int width = 0;
    const char *p = text;
    while (*p) {
        u32 cp = utf8_next(&p);
        FT_Face face = ft_face_for_cp(cp, font_size);
        FT_UInt glyph = FT_Get_Char_Index(face, cp);
        if (!glyph) glyph = FT_Get_Char_Index(face, '?');
        if (glyph && FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0)
            width += (int)(face->glyph->advance.x >> 6);
    }

    if (victim) {
        free(victim->text);
        victim->text = NULL;
        size_t klen = strlen(text) + 1;
        victim->text = (char *)malloc(klen);
        if (victim->text) {
            memcpy(victim->text, text, klen);
            victim->size = font_size;
            victim->width = width;
            victim->used = ++g_ft_mcache_clock;
        }
    }
    return (i64)width;
}
#endif

EXPORT void zan_gui_draw_text(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;
    ZAN_BE(s, draw_text, s, (int)x, (int)y, text, (u32)color, (int)font_size);
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size, 0);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

/* Rotated text: anchor and angle convention match the Win32 driver --
 * (x, y) is the unrotated line box's top-left and the run rotates rigidly
 * about it, positive = clockwise. FreeType bakes the rotation into the
 * cached glyph tiles; the 6x10 bitmap fallback has no rotated rasters and
 * draws unrotated rather than not at all. */
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
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size, angle);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

EXPORT i32 zan_gui_measure_text(const char *text, i32 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) return ft_measure_text(text, (int)font_size);
#endif
    return bitmap_measure_text(text, font_size);
}

EXPORT i32 zan_gui_font_height(i32 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size))
        return (i64)(g_ft_face->size->metrics.height >> 6);
#endif
    return bitmap_font_height(font_size);
}

#endif /* software bitmap text */

/* Icon glyphs are drawn in Zan, as vector primitives on top of the Canvas
 * line/rect/circle/sector calls: stdlib/Gui/IconVector.zan. */

#if defined(__linux__) && !defined(ZAN_GUI_SDL)
/* ---- window management (EWMH / Xlib) ---- */

EXPORT i32 zan_gui_minimize(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XIconifyWindow(g_display, xid, DefaultScreen(g_display));
        XFlush(g_display);
    }
    return 0;
}

EXPORT i32 zan_gui_toggle_maximize(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 x11_atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2 /* toggle */);
    return 0;
}

EXPORT i32 zan_gui_is_maximized(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 0;
    Atom vert = x11_atom("_NET_WM_STATE_MAXIMIZED_VERT");
    Atom horz = x11_atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    Atom actual_type;
    int actual_format;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;
    int found_v = 0, found_h = 0;
    if (XGetWindowProperty(g_display, xid, x11_atom("_NET_WM_STATE"),
                           0, 64, False, XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success && prop) {
        Atom *states = (Atom *)prop;
        for (unsigned long i = 0; i < nitems; i++) {
            if (states[i] == vert) found_v = 1;
            if (states[i] == horz) found_h = 1;
        }
        XFree(prop);
    }
    return (found_v && found_h) ? 1 : 0;
}

/* 1 while the window can be seen (not iconified/unmapped); ambient
 * animations pause while this reports 0. */
EXPORT i32 zan_gui_window_visible(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 0;
    XWindowAttributes attrs;
    if (XGetWindowAttributes(g_display, xid, &attrs)
        && attrs.map_state != IsViewable) {
        return 0;
    }
    return 1;
}

/* 1 while the window holds the input focus; ambient animations idle down to a
 * slow heartbeat while this reports 0, so a background window costs almost
 * nothing. */
EXPORT i32 zan_gui_window_focused(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 0;
    Window focused = 0;
    int revert = 0;
    if (!XGetInputFocus(g_display, &focused, &revert)) return 1;
    return focused == xid ? 1 : 0;
}

EXPORT i32 zan_gui_set_topmost(iptr hwnd_val, i32 on) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_ABOVE"), 0, on ? 1 : 0);
    return 0;
}

/* ---- client-side title-bar metrics (borderless window) ---- */
EXPORT i32 zan_gui_titlebar_height(void) { return g_titlebar_h_l; }
EXPORT i32 zan_gui_caption_button_width(void) { return g_btn_w_l; }
EXPORT i32 zan_gui_set_caption_buttons(iptr hwnd_val, i32 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count_l = (int)count; }
    return 0;
}

/* Close is a *request*, mirroring WM_CLOSE on Win32 and WM_DELETE_WINDOW
 * from the window manager: post a kind-8 event stamped with the target
 * window and let the owner tear the window down (zan_gui_destroy_window)
 * once its loop has seen the event. Destroying synchronously here races the
 * caller's frame loop, which may still render chrome against the dead
 * Window and die with a fatal BadWindow. */
EXPORT i32 zan_gui_close_window(iptr hwnd_val) {
    if (!g_display) return 0;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 0;
    Window prev = g_evwin_linux;
    g_evwin_linux = xid;
    evq_push_linux(8, 0, 0, 0, 0, 0);
    g_evwin_linux = prev;
    zan_gui_wake();
    return 0;
}

EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    if (!g_display) return 0;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    zan_lwin_t *w = lwin_find(xid);
    if (w) {
        zan_gui_release_window((void *)(intptr_t)w->xid);
        if (w->backbuf) XFreePixmap(g_display, w->backbuf);
        if (w->gc) XFreeGC(g_display, w->gc);
        if (w->xic) XDestroyIC(w->xic);
        XDestroyWindow(g_display, w->xid);
        int idx = (int)(w - g_lwins);
        g_lwins[idx] = g_lwins[--g_lwin_count];
    }
    if (xid == g_primary_win) {
        /* Promote another window to primary so process-wide ops keep working. */
        g_primary_win = g_lwin_count ? g_lwins[0].xid : 0;
        g_x11_window = g_primary_win;
        if (g_lwin_count) {
            g_xic = g_lwins[0].xic;
            g_win_w = g_lwins[0].w;
            g_win_h = g_lwins[0].h;
        } else {
            g_xic = NULL;
        }
    }
    if (g_lwin_count == 0) {
        /* Last window gone: tear down shared resources. */
        for (int i = 0; i < 8; i++) {
            if (g_cursors_linux[i]) {
                XFreeCursor(g_display, g_cursors_linux[i]);
                g_cursors_linux[i] = 0;
            }
        }
        if (g_xim) { XCloseIM(g_xim); g_xim = NULL; }
        free(g_clip_text_linux);
        g_clip_text_linux = NULL;
        free(g_clip_read_linux);
        g_clip_read_linux = NULL;
    }
    XFlush(g_display);
    return 0;
}

/* Native glass on Linux: ask a compositing WM (KWin, or picom via rules) to
 * blur whatever is behind the window by setting the de-facto standard
 * _KDE_NET_WM_BLUR_BEHIND_REGION property. An empty region means "blur the
 * whole window". The blur is only *visible* where the window is translucent,
 * which requires a 32-bit ARGB visual plus a running compositor; without those
 * the hint is a harmless no-op. tint is unused (the compositor owns the tint).
 * This is the Linux side of the same Gui.Native.Window.EnableGlass API. */
EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) {
    (void)tint_argb;
    if (!g_display) return 1;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 1;
    Atom blur = x11_atom("_KDE_NET_WM_BLUR_BEHIND_REGION");
    long empty = 0;
    XChangeProperty(g_display, xid, blur, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&empty, 0);
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_disable_glass(iptr hwnd_val) {
    if (!g_display) return 1;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 1;
    Atom blur = x11_atom("_KDE_NET_WM_BLUR_BEHIND_REGION");
    XDeleteProperty(g_display, xid, blur);
    XFlush(g_display);
    return 0;
}

/* Whole-window opacity via the EWMH _NET_WM_WINDOW_OPACITY hint (honoured by
 * any compositing WM). percent is 10..100. */
EXPORT i32 zan_gui_set_opacity(iptr hwnd_val, i32 percent) {
    if (!g_display) return 1;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 1;
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    Atom op = x11_atom("_NET_WM_WINDOW_OPACITY");
    if (percent >= 100) {
        XDeleteProperty(g_display, xid, op);
    } else {
        unsigned long v =
            (unsigned long)((double)percent / 100.0 * 4294967295.0);
        XChangeProperty(g_display, xid, op, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&v, 1);
    }
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_get_dpi_scale(void) {
    if (!g_display) {
        g_display = XOpenDisplay(NULL);
        if (!g_display) return 100;
    }
    int screen = DefaultScreen(g_display);
    int wpx = DisplayWidth(g_display, screen);
    int wmm = DisplayWidthMM(g_display, screen);
    if (wmm <= 0) {
        x11_set_scale_metrics(100);
        return 100;
    }
    double dpi = (double)wpx * 25.4 / (double)wmm;
    long scale = (long)(dpi * 100.0 / 96.0 + 0.5);
    if (scale < 100) scale = 100; /* never report sub-1x scaling */
    x11_set_scale_metrics((int)scale);
    return (i64)scale;
}

EXPORT i32 zan_gui_set_clipboard(const char *utf8) {
    if (!g_display || !g_x11_window) return 1;
    free(g_clip_text_linux);
    g_clip_text_linux = utf8 ? strdup(utf8) : strdup("");
    XSetSelectionOwner(g_display, x11_atom("CLIPBOARD"), g_x11_window, CurrentTime);
    XSetSelectionOwner(g_display, XA_PRIMARY, g_x11_window, CurrentTime);
    XFlush(g_display);
    return 0;
}

typedef struct {
    Atom selection;
    Window requestor;
} x11_selection_match_t;

static Bool x11_selection_notify(Display *display, XEvent *event, XPointer arg) {
    (void)display;
    x11_selection_match_t *match = (x11_selection_match_t *)arg;
    return event->type == SelectionNotify &&
           event->xselection.selection == match->selection &&
           event->xselection.requestor == match->requestor;
}

static int x11_read_selection(Atom selection, Atom target, char **out) {
    Atom property = x11_atom("ZAN_GUI_CLIPBOARD");
    XDeleteProperty(g_display, g_x11_window, property);
    XConvertSelection(g_display, selection, target, property,
                      g_x11_window, CurrentTime);
    XFlush(g_display);

    i64 deadline = zan_gui_get_tick_ms() + 1000;
    x11_selection_match_t match = { selection, g_x11_window };
    for (;;) {
        XEvent ev;
        while (XCheckTypedEvent(g_display, SelectionRequest, &ev)) {
            x11_serve_selection(&ev.xselectionrequest);
        }
        if (XCheckIfEvent(g_display, &ev, x11_selection_notify,
                          (XPointer)&match)) {
            if (ev.xselection.property == None) return 0;
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long nitems = 0, bytes_after = 0;
            unsigned char *data = NULL;
            int rc = XGetWindowProperty(
                g_display, g_x11_window, property, 0, 4 * 1024 * 1024,
                True, AnyPropertyType, &actual_type, &actual_format,
                &nitems, &bytes_after, &data);
            if (rc != Success || !data || actual_format != 8 ||
                actual_type == x11_atom("INCR") || bytes_after != 0) {
                if (data) XFree(data);
                return 0;
            }
            char *copy = (char *)malloc((size_t)nitems + 1);
            if (!copy) {
                XFree(data);
                return 0;
            }
            memcpy(copy, data, (size_t)nitems);
            copy[nitems] = '\0';
            XFree(data);
            *out = copy;
            return 1;
        }

        i64 remain = deadline - zan_gui_get_tick_ms();
        if (remain <= 0) return 0;
        struct pollfd pfd;
        pfd.fd = ConnectionNumber(g_display);
        pfd.events = POLLIN;
        pfd.revents = 0;
        int wait_ms = remain > 1000 ? 1000 : (int)remain;
        if (poll(&pfd, 1, wait_ms) < 0) return 0;
    }
}

EXPORT void zan_gui_set_ime_pos(i32 x, i32 y) {
    /* X11 IME (XIM over-the-spot) not wired yet; accept + ignore. */
    (void)x; (void)y;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    if (!g_display || !g_x11_window) return "";
    Atom clipboard = x11_atom("CLIPBOARD");
    Window owner = XGetSelectionOwner(g_display, clipboard);
    if (owner == g_x11_window)
        return g_clip_text_linux ? g_clip_text_linux : "";
    if (owner == None) return "";

    char *text = NULL;
    if (!x11_read_selection(clipboard, x11_atom("UTF8_STRING"), &text) &&
        !x11_read_selection(clipboard, XA_STRING, &text))
        return "";
    free(g_clip_read_linux);
    g_clip_read_linux = text;
    return g_clip_read_linux;
}
#endif /* __linux__ && !ZAN_GUI_SDL (X11 window management) */
