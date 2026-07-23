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

static int ft_prepare(int font_size) {
    if (g_ft_state == 0) {
        g_ft_state = -1;
        if (!FcInit() || FT_Init_FreeType(&g_ft_library) != 0) return 0;
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
    }
    if (g_ft_state != 1) return 0;
    if (font_size < 8) font_size = 8;
    return FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)font_size) == 0;
}

#define ZAN_FT_FB_MAX 8
static FT_Face g_ft_fb[ZAN_FT_FB_MAX];
static int g_ft_fb_count = 0;

/* Return a face that can render `cp` at `font_size`, discovering a fallback
 * font via fontconfig when the primary "sans" face lacks the glyph (e.g. CJK).
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
    }
    return g_ft_face;
}

static void ft_draw_text(i64 surface_id, i64 x, i64 y,
                         const char *text, i64 color, int font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    int pen_x = (int)x;
    int baseline = (int)y +
                   (int)(g_ft_face->size->metrics.ascender >> 6);
    while (*text) {
        u32 cp = utf8_next(&text);
        FT_Face face = ft_face_for_cp(cp, font_size);
        FT_UInt glyph = FT_Get_Char_Index(face, cp);
        if (!glyph) glyph = FT_Get_Char_Index(face, '?');
        if (glyph && FT_Load_Glyph(face, glyph, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap *bitmap = &slot->bitmap;
            int ox = pen_x + slot->bitmap_left;
            int oy = baseline - slot->bitmap_top;
            int pitch = bitmap->pitch;
            for (int py = 0; py < (int)bitmap->rows; py++) {
                const unsigned char *row =
                    pitch >= 0
                        ? bitmap->buffer + py * pitch
                        : bitmap->buffer +
                              ((int)bitmap->rows - 1 - py) * (-pitch);
                for (int px = 0; px < (int)bitmap->width; px++) {
                    int coverage = 0;
                    if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
                        coverage = row[px];
                    else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
                        coverage = (row[px >> 3] & (0x80 >> (px & 7)))
                                       ? 255
                                       : 0;
                    else if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
                        coverage = row[px * 4 + 3];
                    if (coverage)
                        set_pixel_aa(s, ox + px, oy + py,
                                     (u32)color, coverage);
                }
            }
            pen_x += (int)(slot->advance.x >> 6);
        }
    }
}

static i64 ft_measure_text(const char *text, int font_size) {
    int width = 0;
    while (text && *text) {
        u32 cp = utf8_next(&text);
        FT_Face face = ft_face_for_cp(cp, font_size);
        FT_UInt glyph = FT_Get_Char_Index(face, cp);
        if (!glyph) glyph = FT_Get_Char_Index(face, '?');
        if (glyph && FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0)
            width += (int)(face->glyph->advance.x >> 6);
    }
    return width;
}
#endif

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

EXPORT i64 zan_gui_measure_text(const char *text, i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) return ft_measure_text(text, (int)font_size);
#endif
    return bitmap_measure_text(text, font_size);
}

EXPORT i64 zan_gui_font_height(i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size))
        return (i64)(g_ft_face->size->metrics.height >> 6);
#endif
    return bitmap_font_height(font_size);
}

#endif /* software bitmap text */

/* Icon glyphs are rendered as scalable vector primitives on every platform so
 * they look identical across Windows/macOS/Linux and need no icon font (Segoe
 * MDL2 Assets only exists on Windows 10+, so a font path would break Win7). */
static void icon_line(i64 s, int x0, int y0, int x1, int y1,
                      u32 color, int thickness) {
    zan_gui_draw_line(s, x0, y0, x1, y1, color, thickness);
}

static void icon_circle(i64 s, int cx, int cy, int radius,
                        u32 color, int thickness) {
    const double pi = 3.14159265358979323846;
    int segments = radius > 12 ? 24 : 16;
    int px = cx + radius, py = cy;
    for (int i = 1; i <= segments; i++) {
        double a = 2.0 * pi * (double)i / (double)segments;
        int nx = cx + (int)(cos(a) * radius);
        int ny = cy + (int)(sin(a) * radius);
        icon_line(s, px, py, nx, ny, color, thickness);
        px = nx;
        py = ny;
    }
}

static void icon_arrow(i64 s, int x0, int y0, int x1, int y1,
                       u32 color, int thickness) {
    icon_line(s, x0, y0, x1, y1, color, thickness);
    int dx = x1 - x0, dy = y1 - y0;
    int wing = (abs(dx) + abs(dy)) / 4;
    if (wing < 3) wing = 3;
    if (abs(dx) >= abs(dy)) {
        int sign = dx >= 0 ? 1 : -1;
        icon_line(s, x1, y1, x1 - sign * wing, y1 - wing,
                  color, thickness);
        icon_line(s, x1, y1, x1 - sign * wing, y1 + wing,
                  color, thickness);
    } else {
        int sign = dy >= 0 ? 1 : -1;
        icon_line(s, x1, y1, x1 - wing, y1 - sign * wing,
                  color, thickness);
        icon_line(s, x1, y1, x1 + wing, y1 - sign * wing,
                  color, thickness);
    }
}

static void icon_star(i64 s, int cx, int cy, int radius,
                      u32 color, int thickness) {
    const double pi = 3.14159265358979323846;
    int px = 0, py = 0, first_x = 0, first_y = 0;
    for (int i = 0; i < 10; i++) {
        double a = -pi / 2.0 + (double)i * pi / 5.0;
        int rr = (i & 1) ? radius * 2 / 5 : radius;
        int nx = cx + (int)(cos(a) * rr);
        int ny = cy + (int)(sin(a) * rr);
        if (i == 0) {
            first_x = nx;
            first_y = ny;
        } else {
            icon_line(s, px, py, nx, ny, color, thickness);
        }
        px = nx;
        py = ny;
    }
    icon_line(s, px, py, first_x, first_y, color, thickness);
}

/* Even-odd point-in-polygon test (ray cast). */
static int icon_pt_in_poly(int n, const int *px, const int *py, int x, int y) {
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((py[i] > y) != (py[j] > y)) &&
            (x < (px[j] - px[i]) * (y - py[i]) /
                     (py[j] - py[i]) + px[i])) {
            inside = !inside;
        }
    }
    return inside;
}

/* Solid five-point star (for the "filled" star icon). */
static void icon_fill_star(i64 s, int cx, int cy, int radius, u32 color) {
    const double pi = 3.14159265358979323846;
    int px[10], py[10];
    for (int i = 0; i < 10; i++) {
        double a = -pi / 2.0 + (double)i * pi / 5.0;
        int rr = (i & 1) ? radius * 2 / 5 : radius;
        px[i] = cx + (int)(cos(a) * rr);
        py[i] = cy + (int)(sin(a) * rr);
    }
    for (int yy = cy - radius; yy <= cy + radius; yy++) {
        int run = 0, startx = 0;
        for (int xx = cx - radius; xx <= cx + radius; xx++) {
            if (icon_pt_in_poly(10, px, py, xx, yy)) {
                if (!run) { run = 1; startx = xx; }
            } else if (run) {
                zan_gui_fill_rect(s, startx, yy, xx - startx, 1, color);
                run = 0;
            }
        }
        if (run) {
            zan_gui_fill_rect(s, startx, yy, cx + radius - startx + 1, 1, color);
        }
    }
}

EXPORT void zan_gui_draw_icon(i64 surface_id, i64 x, i64 y, i64 box,
                              i64 color, i64 codepoint) {
    int size = (int)box;
    if (size < 8) size = 8;
    int pad = size / 6;
    int x0 = (int)x + pad, y0 = (int)y + pad;
    int x1 = (int)x + size - pad, y1 = (int)y + size - pad;
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int w = x1 - x0, h = y1 - y0;
    int thick = size / 10;
    if (thick < 1) thick = 1;
    u32 c = (u32)color;

    switch ((int)codepoint) {
    case 0xE8BB: case 0xEA39:
        icon_line(surface_id, x0, y0, x1, y1, c, thick);
        icon_line(surface_id, x1, y0, x0, y1, c, thick);
        break;
    case 0xE921: case 0xE738:
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE922:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        break;
    case 0xE923: case 0xE8C8:
        zan_gui_draw_rect(surface_id, x0 + pad / 2, y0, w - pad / 2,
                          h - pad / 2, c, thick);
        zan_gui_draw_rect(surface_id, x0, y0 + pad / 2, w - pad / 2,
                          h - pad / 2, c, thick);
        break;
    case 0xE73E: case 0xE930:
        icon_line(surface_id, x0, cy, cx - pad / 3, y1, c, thick);
        icon_line(surface_id, cx - pad / 3, y1, x1, y0, c, thick);
        break;
    case 0xE710:
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        break;
    case 0xE76B:
        icon_arrow(surface_id, x1, cy, x0, cy, c, thick);
        break;
    case 0xE76C:
        icon_arrow(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE70E: case 0xE898:
        icon_arrow(surface_id, cx, y1, cx, y0, c, thick);
        break;
    case 0xE70D: case 0xE896:
        icon_arrow(surface_id, cx, y0, cx, y1, c, thick);
        break;
    case 0xE721:
        icon_circle(surface_id, cx - pad / 2, cy - pad / 2,
                    w / 3, c, thick);
        icon_line(surface_id, cx + pad / 2, cy + pad / 2,
                  x1, y1, c, thick);
        break;
    case 0xE713:
        icon_circle(surface_id, cx, cy, w / 4, c, thick);
        for (int i = 0; i < 8; i++) {
            double a = 3.14159265358979323846 * (double)i / 4.0;
            int ax = cx + (int)(cos(a) * w / 3);
            int ay = cy + (int)(sin(a) * h / 3);
            int bx = cx + (int)(cos(a) * w / 2);
            int by = cy + (int)(sin(a) * h / 2);
            icon_line(surface_id, ax, ay, bx, by, c, thick);
        }
        break;
    case 0xE77B:
        zan_gui_fill_circle(surface_id, cx, y0 + h / 4, w / 6, c);
        zan_gui_fill_sector(surface_id, cx, y1, 0, w / 2,
                            270, 450, c);
        break;
    case 0xEA8F:
        icon_circle(surface_id, cx, cy, w / 3, c, thick);
        zan_gui_fill_rect(surface_id, x0, cy, w, h / 3, c);
        zan_gui_fill_circle(surface_id, cx, y1, thick, c);
        break;
    case 0xE7C3:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, x1 - w / 3, y0,
                  x1, y0 + h / 3, c, thick);
        break;
    case 0xE8B7:
        zan_gui_draw_rect(surface_id, x0, y0 + h / 4, w,
                          h * 3 / 4, c, thick);
        icon_line(surface_id, x0, y0 + h / 4, cx, y0 + h / 4, c, thick);
        icon_line(surface_id, x0, y0 + h / 4, x0 + w / 4, y0, c, thick);
        icon_line(surface_id, x0 + w / 4, y0, cx, y0, c, thick);
        break;
    case 0xE80F:
        icon_line(surface_id, x0, cy, cx, y0, c, thick);
        icon_line(surface_id, cx, y0, x1, cy, c, thick);
        icon_line(surface_id, x0 + pad / 2, cy - pad / 2,
                  x0 + pad / 2, y1, c, thick);
        icon_line(surface_id, x1 - pad / 2, cy - pad / 2,
                  x1 - pad / 2, y1, c, thick);
        icon_line(surface_id, x0 + pad / 2, y1,
                  x1 - pad / 2, y1, c, thick);
        break;
    case 0xE74D:
        zan_gui_draw_rect(surface_id, x0 + pad / 2, y0 + pad,
                          w - pad, h - pad, c, thick);
        icon_line(surface_id, x0, y0 + pad / 2, x1, y0 + pad / 2, c, thick);
        icon_line(surface_id, cx - pad, y0, cx + pad, y0, c, thick);
        break;
    case 0xE70F:
        icon_line(surface_id, x0, y1, x1, y0, c, thick + 1);
        icon_line(surface_id, x0, y1, x0 + pad, y1, c, thick);
        break;
    case 0xE946:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx, y0 + h / 4, thick, c);
        icon_line(surface_id, cx, cy - pad / 3, cx, y1 - pad / 3, c, thick);
        break;
    case 0xE7BA:
        icon_line(surface_id, cx, y0, x0, y1, c, thick);
        icon_line(surface_id, x0, y1, x1, y1, c, thick);
        icon_line(surface_id, x1, y1, cx, y0, c, thick);
        icon_line(surface_id, cx, cy - pad / 2, cx, cy + pad / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx, y1 - pad / 2, thick, c);
        break;
    case 0xE735: /* filled star */
        icon_fill_star(surface_id, cx, cy, w / 2, c);
        break;
    case 0xE734: /* empty star */
        icon_star(surface_id, cx, cy, w / 2, c, thick);
        break;
    case 0xEB51:
        icon_line(surface_id, x0, cy - pad, cx, y1, c, thick);
        icon_line(surface_id, cx, y1, x1, cy - pad, c, thick);
        icon_circle(surface_id, x0 + w / 4, cy - pad, w / 4, c, thick);
        icon_circle(surface_id, x1 - w / 4, cy - pad, w / 4, c, thick);
        break;
    case 0xE8E1:
        zan_gui_draw_rect(surface_id, x0 + w / 4, cy - pad,
                          w * 3 / 4, h / 2 + pad, c, thick);
        icon_line(surface_id, x0 + w / 4, cy, x0, cy, c, thick + 1);
        icon_line(surface_id, x0, cy, x0, y1, c, thick + 1);
        break;
    case 0xE787:
        zan_gui_draw_rect(surface_id, x0, y0 + pad / 2, w,
                          h - pad / 2, c, thick);
        icon_line(surface_id, x0, y0 + h / 3, x1, y0 + h / 3, c, thick);
        icon_line(surface_id, x0 + pad, y0, x0 + pad, y0 + pad, c, thick);
        icon_line(surface_id, x1 - pad, y0, x1 - pad, y0 + pad, c, thick);
        break;
    case 0xE823:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_line(surface_id, cx, cy, cx, y0 + pad, c, thick);
        icon_line(surface_id, cx, cy, x1 - pad, cy, c, thick);
        break;
    case 0xEB9F:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_fill_circle(surface_id, x1 - pad, y0 + pad, thick + 1, c);
        icon_line(surface_id, x0 + pad, y1 - pad,
                  cx, cy, c, thick);
        icon_line(surface_id, cx, cy, x1 - pad, y1 - pad, c, thick);
        break;
    case 0xE72C: case 0xE7A7: case 0xE7A6:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_arrow(surface_id, x1, cy, x1 - pad, y0, c, thick);
        break;
    case 0xE712:
        zan_gui_fill_circle(surface_id, x0 + pad, cy, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx, cy, thick + 1, c);
        zan_gui_fill_circle(surface_id, x1 - pad, cy, thick + 1, c);
        break;
    case 0xE700:
        icon_line(surface_id, x0, y0 + pad, x1, y0 + pad, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, x0, y1 - pad, x1, y1 - pad, c, thick);
        break;
    case 0xE7B3:
        icon_line(surface_id, x0, cy, cx, y0 + pad, c, thick);
        icon_line(surface_id, cx, y0 + pad, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, cx, y1 - pad, c, thick);
        icon_line(surface_id, cx, y1 - pad, x0, cy, c, thick);
        zan_gui_fill_circle(surface_id, cx, cy, w / 8, c);
        break;
    case 0xE72E:
        zan_gui_draw_rect(surface_id, x0, cy - pad / 2,
                          w, h / 2 + pad / 2, c, thick);
        icon_circle(surface_id, cx, cy - pad / 2, w / 4, c, thick);
        break;
    case 0xE715:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, x0, y0, cx, cy, c, thick);
        icon_line(surface_id, cx, cy, x1, y0, c, thick);
        break;
    case 0xE706:
        icon_circle(surface_id, cx, cy, w / 4, c, thick);
        for (int i = 0; i < 8; i++) {
            double a = 3.14159265358979323846 * (double)i / 4.0;
            icon_line(surface_id,
                      cx + (int)(cos(a) * w / 3),
                      cy + (int)(sin(a) * h / 3),
                      cx + (int)(cos(a) * w / 2),
                      cy + (int)(sin(a) * h / 2), c, thick);
        }
        break;
    case 0xE708:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_circle(surface_id, cx + pad, cy - pad, w / 2, c, thick);
        break;
    case 0xE718: case 0xE77A:
        zan_gui_fill_circle(surface_id, cx, y0 + h / 3, w / 4, c);
        icon_line(surface_id, cx, cy, cx, y1, c, thick);
        break;
    case 0xE71C:
        icon_line(surface_id, x0, y0, x1, y0, c, thick);
        icon_line(surface_id, x1, y0, cx + pad / 2, cy, c, thick);
        icon_line(surface_id, cx + pad / 2, cy,
                  cx + pad / 2, y1, c, thick);
        break;
    case 0xE8CB:
        icon_arrow(surface_id, x0 + pad, y1, x0 + pad, y0, c, thick);
        icon_arrow(surface_id, x1 - pad, y0, x1 - pad, y1, c, thick);
        break;
    case 0xE80A: case 0xF0E2: case 0xE8A1:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE768:
        icon_line(surface_id, x0 + pad, y0, x0 + pad, y1, c, thick);
        icon_line(surface_id, x0 + pad, y0, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, x0 + pad, y1, c, thick);
        break;
    case 0xE769:
        zan_gui_fill_rect(surface_id, x0 + pad, y0, thick * 2, h, c);
        zan_gui_fill_rect(surface_id, x1 - pad - thick * 2, y0,
                          thick * 2, h, c);
        break;
    case 0xE71A:
        zan_gui_fill_rect(surface_id, x0, y0, w, h, c);
        break;
    case 0xEBE8:
        icon_circle(surface_id, cx, cy, w / 3, c, thick);
        icon_line(surface_id, x0, y0, x0 + pad, cy, c, thick);
        icon_line(surface_id, x1, y0, x1 - pad, cy, c, thick);
        icon_line(surface_id, x0, y1, x0 + pad, cy, c, thick);
        icon_line(surface_id, x1, y1, x1 - pad, cy, c, thick);
        break;
    case 0xE943:
        icon_line(surface_id, cx - pad, y0, x0, cy, c, thick);
        icon_line(surface_id, x0, cy, cx - pad, y1, c, thick);
        icon_line(surface_id, cx + pad, y0, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, cx + pad, y1, c, thick);
        break;
    case 0xE74E:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, y0, w - 2 * pad,
                          h / 3, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, cy, w - 2 * pad,
                          h / 3, c, thick);
        break;
    case 0xE8C6:
        icon_line(surface_id, x0, y0, x1, y1, c, thick);
        icon_line(surface_id, x1, y0, x0, y1, c, thick);
        icon_circle(surface_id, x0 + pad, y1 - pad, pad, c, thick);
        icon_circle(surface_id, x1 - pad, y1 - pad, pad, c, thick);
        break;
    case 0xE77F:
        zan_gui_draw_rect(surface_id, x0, y0 + pad, w, h - pad, c, thick);
        zan_gui_draw_rect(surface_id, cx - pad, y0, pad * 2,
                          pad * 2, c, thick);
        break;
    case 0xE8B3:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, y0 + pad,
                          w - 2 * pad, h - 2 * pad, c, thick);
        break;
    case 0xE8D2:
        icon_line(surface_id, x0, y1, cx, y0, c, thick);
        icon_line(surface_id, cx, y0, x1, y1, c, thick);
        icon_line(surface_id, x0 + pad, cy + pad,
                  x1 - pad, cy + pad, c, thick);
        break;
    case 0xE790:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx - pad, cy - pad, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx + pad, cy - pad, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx, cy + pad, thick + 1, c);
        break;
    case 0xF101: /* chevron-down */
        icon_line(surface_id, x0, cy - h / 6, cx, cy + h / 6, c, thick);
        icon_line(surface_id, cx, cy + h / 6, x1, cy - h / 6, c, thick);
        break;
    case 0xF103: /* chevron-up */
        icon_line(surface_id, x0, cy + h / 6, cx, cy - h / 6, c, thick);
        icon_line(surface_id, cx, cy - h / 6, x1, cy + h / 6, c, thick);
        break;
    case 0xF102: /* chevron-right */
        icon_line(surface_id, cx - w / 6, y0, cx + w / 6, cy, c, thick);
        icon_line(surface_id, cx + w / 6, cy, cx - w / 6, y1, c, thick);
        break;
    case 0xF104: /* chevron-left */
        icon_line(surface_id, cx + w / 6, y0, cx - w / 6, cy, c, thick);
        icon_line(surface_id, cx - w / 6, cy, cx + w / 6, y1, c, thick);
        break;
    case 0xE774: /* globe: circle + equator + meridian + latitude lines */
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        icon_line(surface_id, cx - w / 3, cy - h / 4, cx + w / 3, cy - h / 4, c, thick);
        icon_line(surface_id, cx - w / 3, cy + h / 4, cx + w / 3, cy + h / 4, c, thick);
        break;
    default:
        break;
    }
}

#if defined(__linux__) && !defined(ZAN_GUI_SDL)
/* ---- window management (EWMH / Xlib) ---- */

EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XIconifyWindow(g_display, xid, DefaultScreen(g_display));
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 x11_atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2 /* toggle */);
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
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
EXPORT i64 zan_gui_window_visible(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 0;
    XWindowAttributes attrs;
    if (XGetWindowAttributes(g_display, xid, &attrs)
        && attrs.map_state != IsViewable) {
        return 0;
    }
    return 1;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_ABOVE"), 0, on ? 1 : 0);
    return 0;
}

/* ---- client-side title-bar metrics (borderless window) ---- */
EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h_l; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w_l; }
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count_l = (int)count; }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    if (!g_display) return 0;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    zan_lwin_t *w = lwin_find(xid);
    if (w) {
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

/* On Linux close_window already destroys the X window synchronously, so the
 * owner-driven destroy is a no-op kept for FFI symbol parity across
 * backends. */
EXPORT i64 zan_gui_destroy_window(i64 hwnd_val) { (void)hwnd_val; return 0; }

/* Native glass on Linux: ask a compositing WM (KWin, or picom via rules) to
 * blur whatever is behind the window by setting the de-facto standard
 * _KDE_NET_WM_BLUR_BEHIND_REGION property. An empty region means "blur the
 * whole window". The blur is only *visible* where the window is translucent,
 * which requires a 32-bit ARGB visual plus a running compositor; without those
 * the hint is a harmless no-op. tint is unused (the compositor owns the tint).
 * This is the Linux side of the same Gui.Native.Window.EnableGlass API. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
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

EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) {
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
EXPORT i64 zan_gui_set_opacity(i64 hwnd_val, i64 percent) {
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

EXPORT i64 zan_gui_get_dpi_scale(void) {
    if (!g_display) return 100;
    int screen = DefaultScreen(g_display);
    int wpx = DisplayWidth(g_display, screen);
    int wmm = DisplayWidthMM(g_display, screen);
    if (wmm <= 0) return 100;
    double dpi = (double)wpx * 25.4 / (double)wmm;
    long scale = (long)(dpi * 100.0 / 96.0 + 0.5);
    if (scale < 100) scale = 100; /* never report sub-1x scaling */
    return (i64)scale;
}

EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
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

EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
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
