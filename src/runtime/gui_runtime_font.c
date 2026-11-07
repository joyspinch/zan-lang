/* gui_runtime_font */

#if !defined(_WIN32) && !defined(ZAN_GUI_COCOA)
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
     {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00,0x00,0x00},
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
    if (!p || !*p) return 0;
    if (*p < 0x80) {
        *text += 1;
        return *p;
    }
    if ((*p & 0xE0) == 0xC0) {
        if (!p[1] || (p[1] & 0xC0) != 0x80) {
            *text += 1;
            return 0xFFFD;
        }
        u32 cp = ((u32)(p[0] & 0x1F) << 6) | (u32)(p[1] & 0x3F);
        *text += 2;
        return cp >= 0x80 ? cp : 0xFFFD;
    }
    if ((*p & 0xF0) == 0xE0) {
        if (!p[1] || (p[1] & 0xC0) != 0x80 ||
            !p[2] || (p[2] & 0xC0) != 0x80) {
            *text += 1;
            return 0xFFFD;
        }
        u32 cp = ((u32)(p[0] & 0x0F) << 12) |
                 ((u32)(p[1] & 0x3F) << 6) | (u32)(p[2] & 0x3F);
        *text += 3;
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0xFFFD;
        return cp >= 0x800 ? cp : 0xFFFD;
    }
    if ((*p & 0xF8) == 0xF0) {
        if (!p[1] || (p[1] & 0xC0) != 0x80 ||
            !p[2] || (p[2] & 0xC0) != 0x80 ||
            !p[3] || (p[3] & 0xC0) != 0x80) {
            *text += 1;
            return 0xFFFD;
        }
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

#if defined(__ANDROID__) || defined(__OHOS__)
static char g_android_def_path[256];
static int g_android_def_idx;
static char g_android_cjk_path[256];
static int g_android_cjk_idx;
static int g_android_cjk_ok;

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
    static char buf[1 << 21];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;

    const char *p = buf;
    while ((p = strstr(p, "<family")) != NULL) {
        p += 7;
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
#if defined(__wasm__)
        if (FT_New_Face(g_ft_library, "/fonts/ui.ttf", 0, &g_ft_face) == 0) {
            g_ft_state = 1;
        }
#elif defined(__ANDROID__) || defined(__OHOS__)
        ft_android_pick_fonts();
        if (access(g_android_def_path, R_OK) == 0 &&
            FT_New_Face(g_ft_library, g_android_def_path,
                        (FT_Long)g_android_def_idx, &g_ft_face) == 0) {
            g_ft_state = 1;
        } else {
            static const char *const prim_paths[] = {
                "/system/fonts/HarmonyOS_Sans.ttf",
                "/system/fonts/DroidSans.ttf",
                "/system/fonts/NotoSansCJK-Regular.ttc",
            };
            static const int prim_idx[] = { 0, 0, 2 };
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

#define ZAN_FT_FB_MAX 12

#if defined(__ANDROID__)
#include <android/log.h>
#define FT_LOG(...) __android_log_print(ANDROID_LOG_INFO, "zan_font", __VA_ARGS__)
#else
#define FT_LOG(...) do {} while (0)
#endif
static FT_Face g_ft_fb[ZAN_FT_FB_MAX];
static int g_ft_fb_count = 0;

static FT_Face ft_face_for_cp(u32 cp, int font_size) {
    if (FT_Get_Char_Index(g_ft_face, cp)) return g_ft_face;
    for (int i = 0; i < g_ft_fb_count; i++) {
        if (g_ft_fb[i] && FT_Get_Char_Index(g_ft_fb[i], cp)) {
            FT_Set_Pixel_Sizes(g_ft_fb[i], 0, (FT_UInt)font_size);
            return g_ft_fb[i];
        }
    }
    if (g_ft_fb_count < ZAN_FT_FB_MAX) {
#if defined(__ANDROID__) || defined(__OHOS__)
        const char *fb_paths[4];
        int fb_idx[4];
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
        fb_paths[nfb] = "/system/fonts/NotoColorEmoji.ttf";
        fb_idx[nfb] = 0;
        nfb++;
        for (int i = 0; i < nfb; i++) {
            FT_Face face = NULL;
            FT_Error ferr = access(fb_paths[i], R_OK) == 0
                ? FT_New_Face(g_ft_library, fb_paths[i], fb_idx[i], &face)
                : 0x9C;
            FT_LOG("fb try %s: new=%d idx=%d hasCp=%u hasColor=%d",
                   fb_paths[i], (int)ferr, fb_idx[i],
                   face ? FT_Get_Char_Index(face, cp) : 0u,
                   face ? (int)FT_HAS_COLOR(face) : -1);
            if (ferr == 0 && face &&
                FT_Get_Char_Index(face, cp)) {
                g_ft_fb[g_ft_fb_count++] = face;
                FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                FT_LOG("fb adopt %s for cp %lx", fb_paths[i],
                       (unsigned long)cp);
                return face;
            }
            if (face) FT_Done_Face(face);
        }
        {
            DIR *d = opendir("/system/fonts");
            if (d) {
                struct dirent *de;
                int tried = 0;
                while (g_ft_fb_count < ZAN_FT_FB_MAX && tried < 96 &&
                       (de = readdir(d)) != NULL) {
                    const char *nm = de->d_name;
                    size_t l = strlen(nm);
                    if (l < 5 || (strcasecmp(nm + l - 4, ".ttf") &&
                                  strcasecmp(nm + l - 4, ".ttc") &&
                                  strcasecmp(nm + l - 5, ".otf")))
                        continue;
                    tried++;
                    char path[256];
                    snprintf(path, sizeof(path), "/system/fonts/%s", nm);
                    FT_Face face = NULL;
                    if (access(path, R_OK) == 0 &&
                        FT_New_Face(g_ft_library, path, 0, &face) == 0) {
                        if (FT_Get_Char_Index(face, cp)) {
                            g_ft_fb[g_ft_fb_count++] = face;
                            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                            closedir(d);
                            return face;
                        }
                        FT_Done_Face(face);
                    }
                }
                closedir(d);
            }
        }
#elif defined(__wasm__)
        static int cjk_tried = 0;
        if (!cjk_tried) {
            cjk_tried = 1;
            FT_Face face = NULL;
            if (FT_New_Face(g_ft_library, "/fonts/cjk.ttf", 0, &face) == 0 &&
                face) {
                g_ft_fb[g_ft_fb_count++] = face;
                if (FT_Get_Char_Index(face, cp)) {
                    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                    return face;
                }
            }
        }
#elif !defined(__wasm__)
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

static void colr1_color(FT_Color *palette, const FT_ColorIndex *ci,
                        FT_Color *out) {
    if (ci->palette_index == 0xFFFF || !palette) {
        out->blue = out->green = out->red = 0xFF;
        out->alpha = 0xFF;
    } else {
        *out = palette[ci->palette_index];
    }
    out->alpha = (FT_Byte)((FT_UInt32)out->alpha *
                           (FT_UInt32)(FT_UInt16)ci->alpha / 16384);
}

static void colr1_fill_color(FT_Face face, FT_Color *palette,
                             FT_OpaquePaint op, FT_Color *out) {
    FT_COLR_Paint p;
    FT_ColorStop stop;
    if (!FT_Get_Paint(face, op, &p)) goto white;
    if (p.format == FT_COLR_PAINTFORMAT_SOLID) {
        colr1_color(palette, &p.u.solid.color, out);
        return;
    }
    if (p.format == FT_COLR_PAINTFORMAT_LINEAR_GRADIENT ||
        p.format == FT_COLR_PAINTFORMAT_RADIAL_GRADIENT ||
        p.format == FT_COLR_PAINTFORMAT_SWEEP_GRADIENT) {
        FT_ColorStopIterator *it =
            &p.u.linear_gradient.colorline.color_stop_iterator;
        if (it->num_color_stops &&
            FT_Get_Colorline_Stops(face, &stop, it)) {
            colr1_color(palette, &stop.color, out);
            return;
        }
    }
white:
    out->blue = out->green = out->red = 0xFF;
    out->alpha = 0xFF;
}

static void colr1_paint(FT_Face face, FT_Color *palette, FT_OpaquePaint op,
                        FT_Bitmap *target, FT_Vector *toff, int depth) {
    FT_COLR_Paint p;
    if (depth > 32 || !FT_Get_Paint(face, op, &p)) return;
    switch (p.format) {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
        FT_LayerIterator it = p.u.colr_layers.layer_iterator;
        FT_OpaquePaint child;
        while (FT_Get_Paint_Layers(face, &it, &child))
            colr1_paint(face, palette, child, target, toff, depth + 1);
        break;
    }
    case FT_COLR_PAINTFORMAT_GLYPH: {
        FT_Color c;
        colr1_fill_color(face, palette, p.u.glyph.paint, &c);
        if (FT_Load_Glyph(face, p.u.glyph.glyphID, FT_LOAD_RENDER) == 0) {
            FT_Vector soff;
            soff.x = face->glyph->bitmap_left << 6;
            soff.y = face->glyph->bitmap_top << 6;
            FT_Bitmap_Blend(g_ft_library, &face->glyph->bitmap, soff,
                            target, toff, c);
        }
        break;
    }
    case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
        FT_OpaquePaint child;
        memset(&child, 0, sizeof(child));
        if (FT_Get_Color_Glyph_Paint(face, p.u.colr_glyph.glyphID,
                                     FT_COLOR_NO_ROOT_TRANSFORM, &child))
            colr1_paint(face, palette, child, target, toff, depth + 1);
        break;
    }
    case FT_COLR_PAINTFORMAT_COMPOSITE: {
        int mode = (int)p.u.composite.composite_mode;
        if (mode == FT_COLR_COMPOSITE_SRC) {
            colr1_paint(face, palette, p.u.composite.source_paint,
                        target, toff, depth + 1);
        } else if (mode == FT_COLR_COMPOSITE_DEST) {
            colr1_paint(face, palette, p.u.composite.backdrop_paint,
                        target, toff, depth + 1);
        } else if (mode == FT_COLR_COMPOSITE_DEST_OVER) {
            colr1_paint(face, palette, p.u.composite.source_paint,
                        target, toff, depth + 1);
            colr1_paint(face, palette, p.u.composite.backdrop_paint,
                        target, toff, depth + 1);
        } else {
            colr1_paint(face, palette, p.u.composite.backdrop_paint,
                        target, toff, depth + 1);
            colr1_paint(face, palette, p.u.composite.source_paint,
                        target, toff, depth + 1);
        }
        break;
    }
    case FT_COLR_PAINTFORMAT_TRANSFORM:
        colr1_paint(face, palette, p.u.transform.paint,
                    target, toff, depth + 1);
        break;
    case FT_COLR_PAINTFORMAT_TRANSLATE:
        colr1_paint(face, palette, p.u.translate.paint,
                    target, toff, depth + 1);
        break;
    case FT_COLR_PAINTFORMAT_SCALE:
        colr1_paint(face, palette, p.u.scale.paint,
                    target, toff, depth + 1);
        break;
    case FT_COLR_PAINTFORMAT_ROTATE:
        colr1_paint(face, palette, p.u.rotate.paint,
                    target, toff, depth + 1);
        break;
    case FT_COLR_PAINTFORMAT_SKEW:
        colr1_paint(face, palette, p.u.skew.paint,
                    target, toff, depth + 1);
        break;
    default:
        break;
    }
}

static const zan_glyph_tile *ft_colr_tile(FT_Face face, FT_UInt glyph,
                                          int font_size, const char *key) {
    FT_Color *palette = NULL;
    FT_LayerIterator it;
    FT_UInt lg = glyph, lc = 0;
    FT_Bitmap target;
    FT_Vector toff, soff;
    int layers = 0;
    int advance = 0;
    int w, h, pitch;
    int nz = 0;
    unsigned char *px;
    const zan_glyph_tile *tile = NULL;
    FT_OpaquePaint root;
    int used_v1 = 0;

    memset(&root, 0, sizeof(root));

    if (FT_Palette_Select(face, 0, &palette) != 0) palette = NULL;
    if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0)
        advance = (int)(face->glyph->advance.x >> 6);

    memset(&target, 0, sizeof(target));
    FT_Bitmap_Init(&target);
    toff.x = toff.y = 0;

    if (FT_Get_Color_Glyph_Paint(face, glyph,
                                 FT_COLOR_NO_ROOT_TRANSFORM, &root)) {
        colr1_paint(face, palette, root, &target, &toff, 0);
        used_v1 = 1;
        layers = 1;
    } else {
        FT_LOG("colr v1 miss: glyph=%u pal=%d", glyph, palette ? 1 : 0);
    }

    if (!used_v1) {
        it.p = NULL;
        while (FT_Get_Color_Glyph_Layer(face, glyph, &lg, &lc, &it)) {
            FT_Color c;
            if (lc != 0xFFFF && palette) {
                c = palette[lc];
            } else {
                c.blue = c.green = c.red = 0xFF; c.alpha = 0xFF;
            }
            if (FT_Load_Glyph(face, lg, FT_LOAD_RENDER) == 0) {
                soff.x = face->glyph->bitmap_left << 6;
                soff.y = face->glyph->bitmap_top << 6;
                if (FT_Bitmap_Blend(g_ft_library, &face->glyph->bitmap, soff,
                                    &target, &toff, c) == 0)
                    layers++;
            }
            if (!lg) break;
        }
    }
    if (!layers) { FT_Bitmap_Done(g_ft_library, &target); return NULL; }

    w = (int)target.width;
    h = (int)target.rows;
    pitch = target.pitch;
    FT_LOG("colr glyph=%u w=%d h=%d mode=%d",
           glyph, w, h, (int)target.pixel_mode);
    px = (unsigned char *)malloc((size_t)w * h * 4);
    if (px) {
        for (int py = 0; py < h; py++) {
            const unsigned char *row =
                pitch >= 0 ? target.buffer + (size_t)py * pitch
                           : target.buffer + (size_t)(h - 1 - py) * (-pitch);
            unsigned char *dst = px + (size_t)py * (size_t)w * 4;
            for (int pxi = 0; pxi < w; pxi++) {
                int b = row[pxi * 4], g = row[pxi * 4 + 1];
                int r = row[pxi * 4 + 2], a = row[pxi * 4 + 3];
                if (a) {
                    /* pre-multiplied -> straight, clamped */
                    b = b * 255 / a; g = g * 255 / a; r = r * 255 / a;
                    if (b > 255) b = 255;
                    if (g > 255) g = 255;
                    if (r > 255) r = 255;
                } else {
                    b = g = r = 0;
                }
                dst[pxi * 4] = (unsigned char)b;
                dst[pxi * 4 + 1] = (unsigned char)g;
                dst[pxi * 4 + 2] = (unsigned char)r;
                dst[pxi * 4 + 3] = (unsigned char)a;
                nz += (a > 0);
            }
        }
        FT_LOG("colr alpha-nonzero=%d", nz);
        tile = zan_atlas_store(ZAN_TILE_GLYPH, font_size, key, 6,
                               w, h, (int)(toff.x >> 6), (int)(toff.y >> 6),
                               advance, px, 4);
        if (tile) ((zan_glyph_tile *)tile)->flags = ZAN_TILE_RGBA;
        free(px);
    }
    FT_Bitmap_Done(g_ft_library, &target);
    return tile;
}

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
    if (FT_HAS_COLOR(face) && angle == 0) {
        const zan_glyph_tile *ct = ft_colr_tile(face, glyph, font_size, key);
        if (ct) return ct;
    }
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
    int loaded = glyph && FT_Load_Glyph(face, glyph,
                                        FT_LOAD_RENDER | FT_LOAD_COLOR) == 0;
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

    if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        int pitch = bitmap->pitch;
        unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
        if (!px) return NULL;
        for (int py = 0; py < h; py++) {
            const unsigned char *row =
                pitch >= 0 ? bitmap->buffer + py * pitch
                           : bitmap->buffer + (h - 1 - py) * (-pitch);
            memcpy(px + (size_t)py * (size_t)w * 4, row, (size_t)w * 4);
        }
        const zan_glyph_tile *tile =
            zan_atlas_store(ZAN_TILE_GLYPH, font_size, key, 6,
                            w, h, slot->bitmap_left, slot->bitmap_top,
                            advance, px, 4);
        free(px);
        if (tile) ((zan_glyph_tile *)tile)->flags = ZAN_TILE_RGBA;
        return tile;
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
            if (!tile) continue;
            zan_glyph_batch_add(&batch, tile, pen_x + tile->left,
                                baseline - tile->top);
            pen_x += tile->advance;
        }
        zan_glyph_batch_flush(&batch);
        return;
    }
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
        s_acc += tile->advance;
    }
    zan_glyph_batch_flush(&batch);
}

typedef struct {
    char    *text;
    int      size;
    int      width;
    uint64_t used;
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
#if defined(ZAN_GUI_IOS)
    if (zan_ios_draw_text(surface_id, x, y, text, color, font_size, 0)) return;
#endif
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size, 0);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

EXPORT void zan_gui_draw_text_bold(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size) {
    zan_gui_draw_text(surface_id, x, y, text, color, font_size);
}

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
#if defined(ZAN_GUI_IOS)
    if (zan_ios_draw_text(surface_id, x, y, text, color, font_size, angle)) return;
#endif
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size, angle);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

EXPORT i32 zan_gui_measure_text(const char *text, i32 font_size) {
#if defined(ZAN_GUI_IOS)
    i32 m = zan_ios_measure_text(text, font_size);
    if (m >= 0) return m;
#endif
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) return ft_measure_text(text, (int)font_size);
#endif
    return bitmap_measure_text(text, font_size);
}

EXPORT i32 zan_gui_font_height(i32 font_size) {
#if defined(ZAN_GUI_IOS)
    i32 h = zan_ios_font_height(font_size);
    if (h > 0) return h;
#endif
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        FT_Size_Metrics m = g_ft_face->size->metrics;
        i32 asc = (i32)(m.ascender >> 6);
        i32 desc = (i32)((0 - m.descender) >> 6);
        return (i64)(asc + desc);
    }
#endif
    return bitmap_font_height(font_size);
}

EXPORT i32 zan_gui_font_ascent(i32 font_size) {
#if defined(ZAN_GUI_IOS)
    i32 a = zan_ios_font_ascent(font_size);
    if (a > 0) return a;
#endif
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size))
        return (i64)(g_ft_face->size->metrics.ascender >> 6);
#endif
    return bitmap_font_height(font_size);
}

#endif
