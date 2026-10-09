/* gui_runtime_shims */

/* Embedded WebView (native browser control) */
#if defined(__APPLE__) && !defined(ZAN_GUI_COCOA)
/* profile_id selects a per-account isolation profile (see WebView */
EXPORT i32 zan_gui_webview_create(iptr hwnd, const char *profile_id) {
    (void)hwnd; (void)profile_id; return 0;
}
EXPORT void zan_gui_webview_destroy(i32 h) { (void)h; }
EXPORT void zan_gui_webview_set_frame(i32 h, i32 x, i32 y, i32 w, i32 hh) {
    (void)h; (void)x; (void)y; (void)w; (void)hh;
}
EXPORT void zan_gui_webview_set_visible(i32 h, i32 visible) {
    (void)h; (void)visible;
}
EXPORT void zan_gui_webview_set_clip(i32 h, const char *spec) {
    (void)h; (void)spec;
}
EXPORT void zan_gui_webview_navigate(i32 h, const char *url) {
    (void)h; (void)url;
}
EXPORT void zan_gui_webview_load_html(i32 h, const char *html, const char *base_url) {
    (void)h; (void)html; (void)base_url;
}
EXPORT void zan_gui_webview_back(i32 h) { (void)h; }
EXPORT void zan_gui_webview_forward(i32 h) { (void)h; }
EXPORT void zan_gui_webview_reload(i32 h) { (void)h; }
EXPORT void zan_gui_webview_stop(i32 h) { (void)h; }
EXPORT i32 zan_gui_webview_can_go_back(i32 h) { (void)h; return 0; }
EXPORT i32 zan_gui_webview_can_go_forward(i32 h) { (void)h; return 0; }
EXPORT i32 zan_gui_webview_is_loading(i32 h) { (void)h; return 0; }
EXPORT i32 zan_gui_webview_nav_seq(i32 h) { (void)h; return 0; }
EXPORT i32 zan_gui_webview_last_status(i32 h) { (void)h; return 0; }
EXPORT const char *zan_gui_webview_get_url(i32 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_get_title(i32 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_last_request(i32 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_eval(i32 h, const char *js) {
    (void)h; (void)js; return "";
}
EXPORT const char *zan_gui_webview_get_cookies(i32 h, const char *url) {
    (void)h; (void)url; return "";
}
EXPORT void zan_gui_webview_set_cookie(i32 h, const char *url, const char *name, const char *value) {
    (void)h; (void)url; (void)name; (void)value;
}
EXPORT void zan_gui_webview_clear_cookies(i32 h) { (void)h; }
EXPORT void zan_gui_webview_set_devtools_enabled(i32 h, i32 enabled) {
    (void)h; (void)enabled;
}
EXPORT void zan_gui_webview_set_context_menu_enabled(i32 h, i32 enabled) {
    (void)h; (void)enabled;
}
#endif

/* Win32 wide-string conversion symbols for non-Windows targets */
#if !defined(_WIN32)

/* Decode one UTF-8 sequence at s[*pos) (limit is the span end) */
static int zan_shim_utf8_next(const unsigned char *s, int limit, int *pos,
                              unsigned *cp) {
    unsigned char b = s[*pos];
    int n;
    (*pos)++;
    if (b < 0x80) { *cp = b; return 1; }
    if ((b & 0xE0) == 0xC0) { *cp = b & 0x1Fu; n = 2; }
    else if ((b & 0xF0) == 0xE0) { *cp = b & 0x0Fu; n = 3; }
    else if ((b & 0xF8) == 0xF0) { *cp = b & 0x07u; n = 4; }
    else { *cp = 0xFFFDu; return 0; }
    if (*pos + (n - 1) > limit) { *cp = 0xFFFDu; return 0; }
    for (int k = 1; k < n; k++) {
        unsigned char c = s[*pos];
        if ((c & 0xC0) != 0x80) { *cp = 0xFFFDu; *pos = *pos - k + 1; return 0; }
        *cp = (*cp << 6) | (c & 0x3Fu);
        (*pos)++;
    }
    if ((*cp == 0x80u && n > 1) || (*cp >= 0xD800u && *cp <= 0xDFFFu)
        || *cp > 0x10FFFFu) {
        *cp = 0xFFFDu;
    }
    return n;
}

int MultiByteToWideChar(unsigned int page, unsigned int flags,
                        const void *src, int srcBytes,
                        void *dst, int dstChars) {
    const unsigned char *s = (const unsigned char *)src;
    unsigned short *w = (unsigned short *)dst;
    if (page != 65001u || s == 0 || srcBytes == 0) { return 0; }
    int limit = srcBytes;
    if (limit < 0) {
        limit = 0;
        while (s[limit] != 0) { limit++; }
        limit++;                                    /* include the NUL */
    }
    int i = 0, need = 0, first = (dst == 0);
    int out = 0;
    for (;;) {
        if (i >= limit) { break; }
        unsigned cp;
        int n = zan_shim_utf8_next(s, limit, &i, &cp);
        (void)n;
        if (!first) {
            if (cp >= 0x10000u) {
                if (out + 2 > dstChars) { return out; }
                cp -= 0x10000u;
                w[out++] = (unsigned short)(0xD800u | (cp >> 10));
                w[out++] = (unsigned short)(0xDC00u | (cp & 0x3FFu));
            } else {
                if (out + 1 > dstChars) { return out; }
                w[out++] = (unsigned short)cp;
            }
        } else {
            need += (cp >= 0x10000u) ? 2 : 1;
        }
    }
    return first ? need : out;
}

int WideCharToMultiByte(unsigned int page, unsigned int flags,
                        const void *src, int srcChars,
                        void *dst, int dstBytes,
                        const void *defChar, void *usedDef) {
    const unsigned short *w = (const unsigned short *)src;
    unsigned char *d = (unsigned char *)dst;
    if (page != 65001u || w == 0 || srcChars == 0) { return 0; }
    int limit = srcChars;
    if (limit < 0) {
        limit = 0;
        while (w[limit] != 0) { limit++; }
        limit++;                                    /* include the NUL */
    }
    int i = 0, need = 0, first = (dst == 0);
    int out = 0;
    while (i < limit) {
        unsigned u = w[i++];
        unsigned cp;
        if (u >= 0xD800u && u < 0xDC00u && i < limit
            && w[i] >= 0xDC00u && w[i] < 0xE000u) {
            cp = 0x10000u + ((u - 0xD800u) << 10) + (w[i] - 0xDC00u);
            i++;
        } else if (u >= 0xDC00u && u < 0xE000u) {
            cp = 0xFFFDu;                           /* lone low surrogate */
        } else {
            cp = u;
        }
        unsigned char e[4];
        int n;
        if (cp < 0x80u) { e[0] = (unsigned char)cp; n = 1; }
        else if (cp < 0x800u) {
            e[0] = (unsigned char)(0xC0u | (cp >> 6));
            e[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
            n = 2;
        } else if (cp < 0x10000u) {
            e[0] = (unsigned char)(0xE0u | (cp >> 12));
            e[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
            e[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
            n = 3;
        } else {
            e[0] = (unsigned char)(0xF0u | (cp >> 18));
            e[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
            e[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
            e[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
            n = 4;
        }
        if (first) { need += n; continue; }
        if (out + n > dstBytes) { return out; }
        for (int k = 0; k < n; k++) { d[out++] = e[k]; }
    }
    if (!first && usedDef != 0) { *((int *)usedDef) = 0; }
    (void)flags; (void)defChar;
    return first ? need : out;
}

#endif /* !defined(_WIN32) */
