/* ========================================================================= */
/* CoreText Native Font Rasterization on iOS (Full Unicode / CJK / Emoji)   */
/* ========================================================================= */

typedef struct {
    char *key;
    int font_size;
    int tw, th;
    double width;
    unsigned char *mask;
    uint32_t last_used;
} zan_ios_text_entry_t;

#define ZAN_IOS_CACHE_SETS 1024
#define ZAN_IOS_CACHE_WAYS 4
static zan_ios_text_entry_t g_ios_text_cache[ZAN_IOS_CACHE_SETS][ZAN_IOS_CACHE_WAYS];
static uint32_t g_ios_text_clock = 0;

static unsigned int zan_ios_text_hash(const char *s, int font_size) {
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    h ^= (unsigned int)font_size * 2654435761u;
    return h % ZAN_IOS_CACHE_SETS;
}

static zan_ct_font_t g_font_table[128] = {0};
static int g_font_height_table[128] = {0};
static int g_font_ascent_table[128] = {0};
static CGColorSpaceRef g_cs_rgb = NULL;
static zan_cg_color_t g_white_color = NULL;

static zan_ct_font_t zan_ios_get_font(int font_size) {
    if (font_size < 8) font_size = 8;
    if (font_size > 127) font_size = 127;
    if (g_font_table[font_size]) return g_font_table[font_size];

    if (!g_bridge.initialized) zan_ios_init_bridge();
    double size = (double)font_size;
    zan_ct_font_t font = NULL;

    /* 1. UIKit UIFont systemFontOfSize: (toll-free bridged with CTFontRef on iOS) */
    if (g_bridge.objc_getClass && g_bridge.sel_registerName && g_bridge.objc_msgSend) {
        zan_class_t cls_uifont = g_bridge.objc_getClass("UIFont");
        if (cls_uifont) {
            zan_sel_t sel_sys = g_bridge.sel_registerName("systemFontOfSize:");
            if (sel_sys) {
                typedef zan_id_t (*zan_msg_dbl_ret_id_fn)(zan_id_t, zan_sel_t, double);
                zan_msg_dbl_ret_id_fn fn_sys = (zan_msg_dbl_ret_id_fn)g_bridge.objc_msgSend;
                zan_id_t uifont = fn_sys((zan_id_t)cls_uifont, sel_sys, size);
                if (uifont) font = (zan_ct_font_t)uifont;
            }
        }
    }

    /* 2. CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, NULL) */
    if (!font && p_CTFontCreateUIFontForLanguage) {
        font = p_CTFontCreateUIFontForLanguage(3 /* kCTFontUIFontSystem */, size, NULL);
    }

    /* 3. PostScript font names: AppleSystemUIFont, PingFang SC, Helvetica */
    if (!font && p_CTFontCreateWithName && p_CFStringCreateWithCString) {
        const char *cand_names[] = { ".AppleSystemUIFont", "PingFangSC-Regular", "PingFangSC-Light", "PingFangSC-Medium", "HelveticaNeue", "Helvetica", NULL };
        for (int i = 0; cand_names[i]; i++) {
            zan_cf_str_t fname = p_CFStringCreateWithCString(NULL, cand_names[i], 0x08000100);
            if (fname) {
                font = p_CTFontCreateWithName(fname, size, NULL);
                if (p_CFRelease) p_CFRelease(fname);
                if (font) break;
            }
        }
    }

    if (font) {
        if (p_CFRetain) {
            p_CFRetain(font);
        }
        g_font_table[font_size] = font;
        if (p_CTFontGetAscent && p_CTFontGetDescent) {
            double lead = p_CTFontGetLeading ? p_CTFontGetLeading(font) : 0.0;
            if (lead < 0.0) lead = 0.0;
            double asc = p_CTFontGetAscent(font);
            double desc = p_CTFontGetDescent(font);
            g_font_ascent_table[font_size] = (int)ceil(asc);
            g_font_height_table[font_size] = (int)ceil(asc + desc + lead);
        }
    }
    return font;
}

static zan_ct_line_t zan_ios_create_line(const char *utf8, zan_ct_font_t font, zan_cg_color_t white_color) {
    if (!utf8 || !*utf8 || !font || !p_CFStringCreateWithCString ||
        !p_CFAttributedStringCreate || !p_CTLineCreateWithAttributedString) {
        return NULL;
    }
    zan_cf_str_t str = p_CFStringCreateWithCString(NULL, utf8, 0x08000100);
    if (!str) return NULL;

    zan_cf_dict_t attrs = NULL;
    void *font_attr_key = p_kCTFontAttributeName ? *p_kCTFontAttributeName : NULL;
    if (!font_attr_key && p_NSFontAttributeName) font_attr_key = *p_NSFontAttributeName;
    if (!font_attr_key && p_CFStringCreateWithCString) {
        font_attr_key = p_CFStringCreateWithCString(NULL, "NSFont", 0x08000100);
    }

    void *color_attr_key = p_kCTForegroundColorAttributeName ? *p_kCTForegroundColorAttributeName : NULL;
    if (!color_attr_key && p_CFStringCreateWithCString) {
        color_attr_key = p_CFStringCreateWithCString(NULL, "NSColor", 0x08000100);
    }

    if (p_CFDictionaryCreate) {
        const void *keys[2];
        const void *values[2];
        int count = 0;
        if (font_attr_key) {
            keys[count] = font_attr_key;
            values[count] = font;
            count++;
        }
        if (color_attr_key && white_color) {
            keys[count] = color_attr_key;
            values[count] = white_color;
            count++;
        }
        if (count > 0) {
            attrs = p_CFDictionaryCreate(NULL, keys, values, count,
                                         p_kCFTypeDictionaryKeyCallBacks,
                                         p_kCFTypeDictionaryValueCallBacks);
        }
    }

    zan_cf_attr_str_t attr_str = p_CFAttributedStringCreate(NULL, str, attrs);
    zan_ct_line_t line = p_CTLineCreateWithAttributedString(attr_str);

    if (attr_str && p_CFRelease) p_CFRelease(attr_str);
    if (attrs && p_CFRelease) p_CFRelease(attrs);
    if (str && p_CFRelease) p_CFRelease(str);
    return line;
}

static zan_ios_text_entry_t *zan_ios_text_lookup(const char *utf8, int font_size, bool need_mask) {
    if (!g_bridge.initialized) zan_ios_init_bridge();
    if (!utf8 || !*utf8 || !p_CTLineCreateWithAttributedString || !p_CTLineGetTypographicBounds) {
        return NULL;
    }

    unsigned int set = zan_ios_text_hash(utf8, font_size);
    uint32_t now = ++g_ios_text_clock;

    /* 1. 4-way set associative cache lookup */
    zan_ios_text_entry_t *victim = NULL;
    for (int w = 0; w < ZAN_IOS_CACHE_WAYS; w++) {
        zan_ios_text_entry_t *cand = &g_ios_text_cache[set][w];
        if (cand->key && cand->font_size == font_size && strcmp(cand->key, utf8) == 0) {
            cand->last_used = now;
            if (!need_mask || cand->mask) return cand;
            victim = cand;
            break;
        }
        if (!victim || !cand->key || cand->last_used < victim->last_used) {
            victim = cand;
        }
    }

    zan_ct_font_t font = zan_ios_get_font(font_size);
    if (!font) return NULL;

    if (!g_cs_rgb && p_CGColorSpaceCreateDeviceRGB) {
        g_cs_rgb = p_CGColorSpaceCreateDeviceRGB();
    }
    if (!g_white_color && g_cs_rgb && p_CGColorCreate) {
        const double white_rgba[4] = { 1.0, 1.0, 1.0, 1.0 };
        g_white_color = p_CGColorCreate(g_cs_rgb, white_rgba);
    }

    zan_ct_line_t line = zan_ios_create_line(utf8, font, g_white_color);
    if (!line) return NULL;

    double ascent = 0.0, descent = 0.0, leading = 0.0;
    double measured = p_CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    int tw = (int)ceil(measured) + 2;
    int th = (int)ceil(ascent + descent + leading) + 2;
    unsigned char *mask = (victim && victim->key && strcmp(victim->key, utf8) == 0) ? victim->mask : NULL;

    if (need_mask && tw > 0 && th > 0 && p_CGBitmapContextCreate && p_CTLineDraw) {
        if (!mask) mask = (unsigned char *)calloc((size_t)tw * (size_t)th, 1);
        int rendered = 0;

        /* 1. Direct 8-bit Alpha-Only context (canonical iOS format, zero color-conversion overhead) */
        if (!rendered && mask) {
            zan_cg_context_t ctx = p_CGBitmapContextCreate(
                mask, (size_t)tw, (size_t)th, 8, (size_t)tw,
                NULL, 7 /* kCGImageAlphaOnly */);
            if (ctx) {
                if (p_CGContextSetGrayFillColor) {
                    p_CGContextSetGrayFillColor(ctx, 1.0, 1.0);
                }
                if (p_CGContextSetTextPosition) {
                    p_CGContextSetTextPosition(ctx, 1.0, descent + 1.0);
                }
                p_CTLineDraw(line, ctx);
                if (p_CGContextFlush) p_CGContextFlush(ctx);
                if (p_CGContextRelease) p_CGContextRelease(ctx);
                rendered = 1;
            }
        }

        /* 2. DeviceGray fallback */
        CGColorSpaceRef cs_gray = (!rendered && p_CGColorSpaceCreateDeviceGray) ? p_CGColorSpaceCreateDeviceGray() : NULL;
        if (cs_gray && mask) {
            zan_cg_context_t ctx = p_CGBitmapContextCreate(
                mask, (size_t)tw, (size_t)th, 8, (size_t)tw,
                cs_gray, 0 /* kCGImageAlphaNone */);
            if (ctx) {
                if (p_CGContextSetGrayFillColor) {
                    p_CGContextSetGrayFillColor(ctx, 1.0, 1.0);
                }
                if (p_CGContextSetTextPosition) {
                    p_CGContextSetTextPosition(ctx, 1.0, descent + 1.0);
                }
                p_CTLineDraw(line, ctx);
                if (p_CGContextFlush) p_CGContextFlush(ctx);
                if (p_CGContextRelease) p_CGContextRelease(ctx);
                rendered = 1;
            }
            if (p_CGColorSpaceRelease) p_CGColorSpaceRelease(cs_gray);
        }

        /* 3. RGBA fallback */
        if (!rendered && g_cs_rgb && mask) {
            size_t rgba_sz = (size_t)tw * (size_t)th * sizeof(uint32_t);
            uint32_t *rgba_buf = (uint32_t *)calloc(rgba_sz, 1);
            if (rgba_buf) {
                zan_cg_context_t ctx = p_CGBitmapContextCreate(
                    rgba_buf, (size_t)tw, (size_t)th, 8, (size_t)tw * sizeof(uint32_t),
                    g_cs_rgb, (4 << 12) | 1);
                if (ctx) {
                    if (p_CGContextSetTextPosition) {
                        p_CGContextSetTextPosition(ctx, 1.0, descent + 1.0);
                    }
                    p_CTLineDraw(line, ctx);
                    if (p_CGContextFlush) p_CGContextFlush(ctx);
                    if (p_CGContextRelease) p_CGContextRelease(ctx);

                    for (int py = 0; py < th; py++) {
                        const unsigned char *src_row = (const unsigned char *)(rgba_buf + (size_t)py * (size_t)tw);
                        unsigned char *dst_row = mask + (size_t)py * (size_t)tw;
                        for (int px = 0; px < tw; px++) {
                            const unsigned char *p = src_row + (px * 4);
                            unsigned char r = p[0], g = p[1], b = p[2], a = p[3];
                            unsigned char max_c = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
                            dst_row[px] = (a > max_c) ? a : max_c;
                        }
                    }
                    rendered = 1;
                }
                free(rgba_buf);
            }
        }
    }

    if (line && p_CFRelease) {
        p_CFRelease(line);
    }

    if (!victim) victim = &g_ios_text_cache[set][0];
    if (victim->key && victim->key != utf8 && (!victim->mask || victim->mask != mask)) {
        free(victim->key);
        if (victim->mask && victim->mask != mask) free(victim->mask);
    }
    victim->key = strdup(utf8);
    victim->font_size = font_size;
    victim->tw = tw;
    victim->th = th;
    victim->width = measured;
    victim->mask = mask;
    victim->last_used = now;
    return victim;
}

static inline uint32_t zan_ios_blend_pixel(uint32_t dst, uint32_t color, unsigned int coverage) {
    if (coverage == 0) return dst;
    uint32_t ca = (color >> 24) & 0xFF;
    if (ca == 0) ca = 255;
    uint32_t alpha = ca * coverage / 255;
    if (alpha == 0) return dst;
    uint32_t inv = 255 - alpha;
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = color & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;
    uint32_t r = (sr * alpha + dr * inv) / 255;
    uint32_t g = (sg * alpha + dg * inv) / 255;
    uint32_t b = (sb * alpha + db * inv) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

extern uint32_t *zan_gui_internal_surface_data(int64_t id, int *w, int *h, int *stride);
extern void zan_gui_internal_surface_clip(int64_t id, int *x0, int *y0, int *x1, int *y1);

int zan_ios_draw_text(i32 surface_id, i32 x, i32 y, const char *utf8, i32 color, i32 font_size, i32 angle) {
    (void)angle;
    int sw = 0, sh = 0, stride = 0;
    uint32_t *pixels = zan_gui_internal_surface_data((int64_t)surface_id, &sw, &sh, &stride);
    if (!pixels || !utf8 || !*utf8) return 1;

    zan_ios_text_entry_t *e = zan_ios_text_lookup(utf8, (int)font_size, true);
    if (!e || !e->mask) return 0;

    int tw = e->tw, th = e->th;
    const unsigned char *mask = e->mask;

    int cx0 = 0, cy0 = 0, cx1 = sw, cy1 = sh;
    zan_gui_internal_surface_clip((int64_t)surface_id, &cx0, &cy0, &cx1, &cy1);

    for (int py = 0; py < th; py++) {
        int dy = (int)y + py - 1;
        if (dy < cy0 || dy >= cy1) continue;
        uint32_t *dst_row = pixels + (size_t)dy * stride;
        const unsigned char *mask_row = mask + (size_t)py * tw;
        for (int px = 0; px < tw; px++) {
            int dx = (int)x + px - 1;
            if (dx < cx0 || dx >= cx1) continue;
            unsigned char cov = mask_row[px];
            if (cov > 0) {
                dst_row[dx] = zan_ios_blend_pixel(dst_row[dx], (uint32_t)color, cov);
            }
        }
    }
    return 1;
}

i32 zan_ios_measure_text(const char *utf8, i32 font_size) {
    if (!utf8 || !*utf8) return 0;
    zan_ios_text_entry_t *e = zan_ios_text_lookup(utf8, (int)font_size, false);
    if (e && e->width > 0.0) return (i32)ceil(e->width);
    int len = 0;
    const char *p = utf8;
    while (*p) {
        if ((*p & 0xC0) != 0x80) len++;
        p++;
    }
    return (i32)(len * font_size * 60 / 100);
}

i32 zan_ios_font_height(i32 font_size) {
    if (font_size < 8) font_size = 8;
    if (font_size > 127) font_size = 127;
    if (g_font_height_table[font_size] > 0) return g_font_height_table[font_size];
    zan_ios_get_font(font_size);
    if (g_font_height_table[font_size] > 0) return g_font_height_table[font_size];
    return (i32)(font_size > 0 ? (font_size * 125 / 100) : 16);
}

i32 zan_ios_font_ascent(i32 font_size) {
    if (font_size < 8) font_size = 8;
    if (font_size > 127) font_size = 127;
    if (g_font_ascent_table[font_size] > 0) return g_font_ascent_table[font_size];
    zan_ios_get_font(font_size);
    if (g_font_ascent_table[font_size] > 0) return g_font_ascent_table[font_size];
    return (i32)(font_size > 0 ? (font_size * 95 / 100) : 12);
}
