/* gui_runtime_tray */

#include "zan_bitmap.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)

#include <pthread.h>

#define ZAN_TRAY_TIP_MAX   256
#define ZAN_TRAY_MENU_MAX  8192
#define ZAN_TRAY_PATH_MAX  1024
#define ZAN_TRAY_QCAP      64
#define ZAN_TRAY_ITEM_MAX  256

/* 模块核心语义抽象与接口调用契约 */
#define ZAN_TRAY_OPCODE_DOCK 0

typedef struct {
    int  id;
    int  flags;         /* 底层系统交互与数据协议契约 */
    char text[128];
} zan_tray_item_t;

/* 模块核心语义抽象与接口调用契约 */
typedef struct {
    char path[ZAN_TRAY_PATH_MAX];
    u32 *pixels;
    int width, height;
} zan_tray_start_t;

static pthread_t       g_tray_thread;
static pthread_mutex_t g_tray_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_tray_cv = PTHREAD_COND_INITIALIZER;
/* 核心系统底层抽象与内存语义契约 */
static int  g_tray_live      = 0;   /* 底层系统交互与数据协议契约 */
static int  g_tray_start_rc  = 0;   /* 0 pending, 1 docked, -1 failed */
static int  g_tray_quit      = 0;
static int  g_tray_ctl[2]    = { -1, -1 };
static char g_tray_tip[ZAN_TRAY_TIP_MAX];
static int  g_tray_tip_dirty = 0;
static char g_tray_menu_spec[ZAN_TRAY_MENU_MAX];
static int  g_tray_menu_dirty = 0;
static int  g_tray_q[ZAN_TRAY_QCAP];
static int  g_tray_qhead = 0, g_tray_qtail = 0;

/* 模块核心语义抽象与接口调用契约 */
static Display *g_tray_dpy      = NULL;
static Window   g_tray_win      = 0;
static Window   g_tray_manager  = 0;
static GC       g_tray_gc       = NULL;
/* 内部辅助逻辑 */
static Font g_tray_font    = 0;
static int  g_tray_font16  = 0;   /* font is 2-byte (iso10646-1) */
static int  g_tray_ascent  = 11;
static u32     *g_tray_pix      = NULL;   /* ARGB32 icon, or NULL */
static int      g_tray_pix_w    = 0, g_tray_pix_h = 0;
static int      g_tray_w        = 24, g_tray_h = 24;
static zan_tray_item_t g_tray_items[ZAN_TRAY_ITEM_MAX];
static int      g_tray_item_n   = 0;
static Time     g_tray_last_click = 0;

/* 核心系统底层抽象与内存语义契约 */

static void tray_push(int ev) {
    pthread_mutex_lock(&g_tray_mu);
    int next = (g_tray_qtail + 1) % ZAN_TRAY_QCAP;
    if (next != g_tray_qhead) {
        g_tray_q[g_tray_qtail] = ev;
        g_tray_qtail = next;
    }
    pthread_cond_broadcast(&g_tray_cv);
    pthread_mutex_unlock(&g_tray_mu);
}

/* 核心系统底层抽象与内存语义契约 */

/* 模块核心语义抽象与接口调用契约 */
static unsigned char *tray_read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || n > (64 << 20)) { fclose(f); return NULL; }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    long got = (long)fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != n) { free(buf); return NULL; }
    *out_len = n;
    return buf;
}

/* 内部辅助逻辑 */
static u32 *tray_copy_bitmap(const zan_bitmap *bitmap, int *w, int *h) {
    u32 *copy;
    int row;
    *w = *h = 0;
    if (!bitmap || !bitmap->pixels || bitmap->width <= 0 || bitmap->height <= 0
        || bitmap->width > 32768 || bitmap->height > 32768
        || bitmap->stride < bitmap->width)
        return NULL;
    if ((size_t)bitmap->width > SIZE_MAX / sizeof(u32) / (size_t)bitmap->height
        || (size_t)(bitmap->height - 1)
            > (SIZE_MAX / sizeof(u32) - (size_t)bitmap->width)
              / (size_t)bitmap->stride)
        return NULL;
    copy = (u32 *)malloc((size_t)bitmap->width * (size_t)bitmap->height * sizeof(u32));
    if (!copy) return NULL;
    for (row = 0; row < bitmap->height; row++) {
        memcpy(copy + (size_t)row * (size_t)bitmap->width,
               bitmap->pixels + (size_t)row * (size_t)bitmap->stride,
               (size_t)bitmap->width * sizeof(u32));
    }
    *w = bitmap->width;
    *h = bitmap->height;
    return copy;
}

static u32 tray_le32(const unsigned char *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* 内部辅助逻辑 */
static u32 *tray_from_ico_dib(const unsigned char *p, long len, int *w, int *h) {
    if (len < 40) return NULL;
    u32 hdr    = tray_le32(p);
    int width  = (int)tray_le32(p + 4);
    int height = (int)tray_le32(p + 8) / 2;   /* 核心系统底层抽象与内存语义契约 */
    int bpp    = (int)(p[14] | (p[15] << 8));
    u32 comp   = tray_le32(p + 16);
    if (hdr < 40 || hdr > (u32)len || comp != 0) return NULL;
    if (width <= 0 || height <= 0 || width > 512 || height > 512) return NULL;
    if (bpp != 32 && bpp != 24) return NULL;
    int bytes_per_px = bpp / 8;
    long row = ((long)width * bytes_per_px + 3) & ~3L;
    if ((long)hdr + row * height > len) return NULL;
    u32 *out = (u32 *)malloc((size_t)width * (size_t)height * 4);
    if (!out) return NULL;
    const unsigned char *bits = p + hdr;
    for (int y = 0; y < height; y++) {
        const unsigned char *src = bits + row * (height - 1 - y);
        for (int x = 0; x < width; x++) {
            const unsigned char *px = src + (long)x * bytes_per_px;
            u32 a = (bpp == 32) ? px[3] : 0xFF;
            out[(long)y * width + x] =
                (a << 24) | ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
        }
    }
    *w = width;
    *h = height;
    return out;
}

/* 内部辅助逻辑 */
static u32 *tray_from_ico(const unsigned char *p, long len, int *w, int *h) {
    if (len < 6 || tray_le32(p) != 0x00010000u) return NULL;
    int count = (int)(p[4] | (p[5] << 8));
    if (count <= 0 || 6 + count * 16 > len) return NULL;
    long best_off = 0, best_len = 0, best_area = 0;
    for (int i = 0; i < count; i++) {
        const unsigned char *e = p + 6 + i * 16;
        int ew = e[0] ? e[0] : 256;
        int eh = e[1] ? e[1] : 256;
        uint32_t size = tray_le32(e + 8);
        uint32_t off = tray_le32(e + 12);
        if (!off || !size || off > (uint32_t)len
            || size > (uint32_t)len - off) continue;
        if ((long)ew * eh > best_area) {
            best_area = (long)ew * eh;
            best_off = (long)off;
            best_len = (long)size;
        }
    }
    if (best_area == 0) return NULL;
    return tray_from_ico_dib(p + best_off, best_len, w, h);
}

/* 模块核心语义抽象与接口调用契约 */
static u32 *tray_load_icon(const char *path, int *w, int *h) {
    if (!path || !*path) return NULL;
    long len = 0;
    unsigned char *data = tray_read_file(path, &len);
    if (!data) return NULL;
    u32 *pix = tray_from_ico(data, len, w, h);
    free(data);
    return pix;
}

/* drawing */

/* 模块核心语义抽象与接口调用契约 */
static void tray_paint(void) {
    if (!g_tray_dpy || !g_tray_win) return;
    XClearWindow(g_tray_dpy, g_tray_win);
    if (!g_tray_pix || g_tray_pix_w <= 0 || g_tray_pix_h <= 0) {
        /* 内部辅助逻辑 */
        XSetForeground(g_tray_dpy, g_tray_gc, 0x3B78FF);
        XFillRectangle(g_tray_dpy, g_tray_win, g_tray_gc, 3, 3,
                       (unsigned)(g_tray_w - 6), (unsigned)(g_tray_h - 6));
        XFlush(g_tray_dpy);
        return;
    }
    for (int y = 0; y < g_tray_h; y++) {
        int sy = y * g_tray_pix_h / g_tray_h;
        int run_x0 = -1;
        u32 run_col = 0;
        for (int x = 0; x <= g_tray_w; x++) {
            int draw = 0;
            u32 col = 0;
            if (x < g_tray_w) {
                int sx = x * g_tray_pix_w / g_tray_w;
                u32 px = g_tray_pix[(long)sy * g_tray_pix_w + sx];
                if ((px >> 24) >= 0x80) { draw = 1; col = px & 0xFFFFFFu; }
            }
            if (draw && run_x0 < 0) { run_x0 = x; run_col = col; }
            else if (draw && col != run_col) {
                XSetForeground(g_tray_dpy, g_tray_gc, run_col);
                XFillRectangle(g_tray_dpy, g_tray_win, g_tray_gc, run_x0, y,
                               (unsigned)(x - run_x0), 1);
                run_x0 = x;
                run_col = col;
            } else if (!draw && run_x0 >= 0) {
                XSetForeground(g_tray_dpy, g_tray_gc, run_col);
                XFillRectangle(g_tray_dpy, g_tray_win, g_tray_gc, run_x0, y,
                               (unsigned)(x - run_x0), 1);
                run_x0 = -1;
            }
        }
    }
    XFlush(g_tray_dpy);
}

static void tray_apply_tooltip(void) {
    char tip[ZAN_TRAY_TIP_MAX];
    pthread_mutex_lock(&g_tray_mu);
    memcpy(tip, g_tray_tip, sizeof(tip));
    g_tray_tip_dirty = 0;
    pthread_mutex_unlock(&g_tray_mu);
    if (!g_tray_win) return;
    /* 模块核心语义抽象与接口调用契约 */
    XStoreName(g_tray_dpy, g_tray_win, tip);
    Atom net_name = XInternAtom(g_tray_dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(g_tray_dpy, "UTF8_STRING", False);
    XChangeProperty(g_tray_dpy, g_tray_win, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char *)tip, (int)strlen(tip));
    XFlush(g_tray_dpy);
}

/* 内部辅助逻辑 */
static void tray_apply_menu(void) {
    char spec[ZAN_TRAY_MENU_MAX];
    pthread_mutex_lock(&g_tray_mu);
    memcpy(spec, g_tray_menu_spec, sizeof(spec));
    g_tray_menu_dirty = 0;
    pthread_mutex_unlock(&g_tray_mu);

    g_tray_item_n = 0;
    const char *p = spec;
    while (*p && g_tray_item_n < ZAN_TRAY_ITEM_MAX) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
        if (line_len > 0) {
            char line[256];
            if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
            memcpy(line, p, line_len);
            line[line_len] = 0;
            char *t1 = strchr(line, '\t');
            char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
            if (t1 && t2) {
                *t1 = 0;
                *t2 = 0;
                zan_tray_item_t *it = &g_tray_items[g_tray_item_n++];
                it->id = atoi(line);
                it->flags = atoi(t1 + 1);
                snprintf(it->text, sizeof(it->text), "%s", t2 + 1);
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
}

/* 内部辅助逻辑 */
static int tray_to_ucs2(const char *s, XChar2b *out, int cap) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < cap) {
        unsigned cp = *p;
        if (cp < 0x80) { p += 1; }
        else if ((cp >> 5) == 0x6 && p[1]) {
            cp = ((cp & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2;
        } else if ((cp >> 4) == 0xE && p[1] && p[2]) {
            cp = ((cp & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
            p += 3;
        } else if ((cp >> 3) == 0x1E && p[1] && p[2] && p[3]) {
            cp = 0xFFFD; p += 4;
        } else { cp = 0xFFFD; p += 1; }
        if (cp > 0xFFFF) cp = 0xFFFD;
        out[n].byte1 = (unsigned char)(cp >> 8);
        out[n].byte2 = (unsigned char)(cp & 0xFF);
        n++;
    }
    return n;
}

#define ZAN_TRAY_GLYPH_MAX 128

static int tray_text_width(const char *s) {
    if (!g_tray_font || !s || !*s) return 0;
    XChar2b buf[ZAN_TRAY_GLYPH_MAX];
    int n;
    if (g_tray_font16) {
        n = tray_to_ucs2(s, buf, ZAN_TRAY_GLYPH_MAX);
    } else {
        /* 模块核心语义抽象与接口调用契约 */
        n = (int)strlen(s);
        if (n > ZAN_TRAY_GLYPH_MAX) n = ZAN_TRAY_GLYPH_MAX;
        for (int i = 0; i < n; i++) {
            buf[i].byte1 = 0;
            buf[i].byte2 = (unsigned char)s[i];
        }
    }
    int dir = 0, fa = 0, fd = 0;
    XCharStruct ov;
    memset(&ov, 0, sizeof(ov));
    if (!XQueryTextExtents16(g_tray_dpy, g_tray_font, buf, n, &dir, &fa, &fd,
                             &ov)) {
        return n * 8;
    }
    if (fa > 0) g_tray_ascent = fa;
    return ov.width;
}

static void tray_draw_text(Window w, int x, int y, const char *s) {
    if (!g_tray_font || !s || !*s) return;
    if (g_tray_font16) {
        XChar2b buf[ZAN_TRAY_GLYPH_MAX];
        int n = tray_to_ucs2(s, buf, ZAN_TRAY_GLYPH_MAX);
        XDrawString16(g_tray_dpy, w, g_tray_gc, x, y, buf, n);
    } else {
        /* 底层系统交互与数据协议契约 */
        XDrawString(g_tray_dpy, w, g_tray_gc, x, y, s, (int)strlen(s));
    }
}

/* 核心系统底层抽象与内存语义契约 */

#define ZAN_TRAY_ITEM_H  22
#define ZAN_TRAY_SEP_H   7
#define ZAN_TRAY_PAD_X   26

static int tray_item_height(const zan_tray_item_t *it) {
    return (it->flags & 4) ? ZAN_TRAY_SEP_H : ZAN_TRAY_ITEM_H;
}

static void tray_draw_menu(Window menu, int width, int height, int hot) {
    XSetForeground(g_tray_dpy, g_tray_gc, 0xFFFFFF);
    XFillRectangle(g_tray_dpy, menu, g_tray_gc, 0, 0, (unsigned)width,
                   (unsigned)height);
    XSetForeground(g_tray_dpy, g_tray_gc, 0x9A9A9A);
    XDrawRectangle(g_tray_dpy, menu, g_tray_gc, 0, 0, (unsigned)(width - 1),
                   (unsigned)(height - 1));
    int y = 1;
    for (int i = 0; i < g_tray_item_n; i++) {
        zan_tray_item_t *it = &g_tray_items[i];
        int ih = tray_item_height(it);
        if (it->flags & 4) {
            XSetForeground(g_tray_dpy, g_tray_gc, 0xD0D0D0);
            XDrawLine(g_tray_dpy, menu, g_tray_gc, 6, y + ih / 2, width - 7,
                      y + ih / 2);
        } else {
            if (i == hot && !(it->flags & 1)) {
                XSetForeground(g_tray_dpy, g_tray_gc, 0xDCE9FF);
                XFillRectangle(g_tray_dpy, menu, g_tray_gc, 1, y,
                               (unsigned)(width - 2), (unsigned)ih);
            }
            if (it->flags & 2) {
                XSetForeground(g_tray_dpy, g_tray_gc, 0x303030);
                XDrawLine(g_tray_dpy, menu, g_tray_gc, 9, y + ih / 2,
                          12, y + ih - 6);
                XDrawLine(g_tray_dpy, menu, g_tray_gc, 12, y + ih - 6,
                          18, y + 5);
            }
            XSetForeground(g_tray_dpy, g_tray_gc,
                           (it->flags & 1) ? 0xA0A0A0 : 0x1A1A1A);
            tray_draw_text(menu, ZAN_TRAY_PAD_X, y + ih - 7, it->text);
        }
        y += ih;
    }
    XFlush(g_tray_dpy);
}

static int tray_hit_item(int y) {
    int top = 1;
    for (int i = 0; i < g_tray_item_n; i++) {
        int ih = tray_item_height(&g_tray_items[i]);
        if (y >= top && y < top + ih) return i;
        top += ih;
    }
    return -1;
}

/* 内部辅助逻辑 */
static int tray_show_menu(void) {
    if (g_tray_item_n == 0) return 0;
    int width = 120, height = 2;
    for (int i = 0; i < g_tray_item_n; i++) {
        int w = tray_text_width(g_tray_items[i].text) + ZAN_TRAY_PAD_X + 18;
        if (w > width) width = w;
        height += tray_item_height(&g_tray_items[i]);
    }
    if (width > 480) width = 480;

    int screen = DefaultScreen(g_tray_dpy);
    int sw = DisplayWidth(g_tray_dpy, screen);
    int sh = DisplayHeight(g_tray_dpy, screen);
    Window root_ret, child_ret;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    XQueryPointer(g_tray_dpy, RootWindow(g_tray_dpy, screen), &root_ret,
                  &child_ret, &rx, &ry, &wx, &wy, &mask);
    int mx = rx, my = ry;
    if (mx + width > sw) mx = sw - width;
    if (my + height > sh) my = sh - height;   /* 核心系统底层抽象与内存语义契约 */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(g_tray_dpy, screen);
    attrs.save_under = True;
    Window menu = XCreateWindow(
        g_tray_dpy, RootWindow(g_tray_dpy, screen), mx, my, (unsigned)width,
        (unsigned)height, 0, CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWSaveUnder, &attrs);
    if (!menu) return 0;
    /* 内部辅助逻辑 */
    Atom wtype = XInternAtom(g_tray_dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom popup = XInternAtom(g_tray_dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    XChangeProperty(g_tray_dpy, menu, wtype, XA_ATOM, 32, PropModeReplace,
                    (const unsigned char *)&popup, 1);
    XSelectInput(g_tray_dpy, menu,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | LeaveWindowMask);
    XMapRaised(g_tray_dpy, menu);
    XGrabPointer(g_tray_dpy, menu, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(g_tray_dpy, menu, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);

    int hot = -1, chosen = 0, done = 0;
    tray_draw_menu(menu, width, height, hot);
    while (!done) {
        XEvent ev;
        XNextEvent(g_tray_dpy, &ev);
        if (ev.xany.window != menu && ev.type != ButtonPress &&
            ev.type != ButtonRelease && ev.type != KeyPress) {
            continue;
        }
        switch (ev.type) {
        case Expose:
            tray_draw_menu(menu, width, height, hot);
            break;
        case MotionNotify: {
            int nh = -1;
            if (ev.xmotion.x >= 0 && ev.xmotion.x < width) {
                nh = tray_hit_item(ev.xmotion.y);
            }
            if (nh != hot) {
                hot = nh;
                tray_draw_menu(menu, width, height, hot);
            }
            break;
        }
        case LeaveNotify:
            if (hot != -1) {
                hot = -1;
                tray_draw_menu(menu, width, height, hot);
            }
            break;
        case KeyPress:
            done = 1;   /* 核心系统底层抽象与内存语义契约 */
            break;
        case ButtonPress:
            if (ev.xbutton.window != menu) { done = 1; }
            break;
        case ButtonRelease: {
            int inside = ev.xbutton.window == menu && ev.xbutton.x >= 0 &&
                         ev.xbutton.x < width && ev.xbutton.y >= 0 &&
                         ev.xbutton.y < height;
            if (!inside) { done = 1; break; }
            int idx = tray_hit_item(ev.xbutton.y);
            if (idx >= 0 && !(g_tray_items[idx].flags & 5)) {
                chosen = g_tray_items[idx].id;
                done = 1;
            }
            break;
        }
        default:
            break;
        }
    }
    XUngrabKeyboard(g_tray_dpy, CurrentTime);
    XUngrabPointer(g_tray_dpy, CurrentTime);
    XDestroyWindow(g_tray_dpy, menu);
    XFlush(g_tray_dpy);
    return chosen;
}

/* docking */

static Window tray_find_manager(void) {
    char sel[64];
    snprintf(sel, sizeof(sel), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(g_tray_dpy));
    Atom atom = XInternAtom(g_tray_dpy, sel, False);
    return XGetSelectionOwner(g_tray_dpy, atom);
}

static int tray_dock(void) {
    g_tray_manager = tray_find_manager();
    if (!g_tray_manager) return 0;
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = g_tray_manager;
    ev.xclient.message_type = XInternAtom(g_tray_dpy, "_NET_SYSTEM_TRAY_OPCODE",
                                          False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)CurrentTime;
    ev.xclient.data.l[1] = ZAN_TRAY_OPCODE_DOCK;
    ev.xclient.data.l[2] = (long)g_tray_win;
    if (!XSendEvent(g_tray_dpy, g_tray_manager, False, NoEventMask, &ev)) {
        return 0;
    }
    /* 内部辅助逻辑 */
    XSelectInput(g_tray_dpy, g_tray_manager, StructureNotifyMask);
    XFlush(g_tray_dpy);
    return 1;
}

static int tray_setup(const char *icon_path) {
    g_tray_dpy = XOpenDisplay(NULL);
    if (!g_tray_dpy) return 0;
    int screen = DefaultScreen(g_tray_dpy);
    Window root = RootWindow(g_tray_dpy, screen);

    XSetWindowAttributes attrs;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       StructureNotifyMask;
    Window win = XCreateWindow(g_tray_dpy, root, 0, 0, (unsigned)g_tray_w,
                               (unsigned)g_tray_h, 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWEventMask,
                               &attrs);
    if (!win) {
        XCloseDisplay(g_tray_dpy);
        g_tray_dpy = NULL;
        return 0;
    }
    g_tray_win = win;
    /* 内部辅助逻辑 */
    XSetWindowBackgroundPixmap(g_tray_dpy, win, ParentRelative);
    g_tray_gc = XCreateGC(g_tray_dpy, win, 0, NULL);

    /* 模块核心语义抽象与接口调用契约 */
    Atom xembed_info = XInternAtom(g_tray_dpy, "_XEMBED_INFO", False);
    unsigned long info[2] = { 0, 1 };
    XChangeProperty(g_tray_dpy, win, xembed_info, xembed_info, 32,
                    PropModeReplace, (const unsigned char *)info, 2);

    /* 内部辅助逻辑 */
    static const char *fonts[] = {
        "-*-*-medium-r-normal--14-*-*-*-*-*-iso10646-1",
        "-*-*-medium-r-normal--*-*-*-*-*-*-iso10646-1",
        "-misc-fixed-medium-r-normal--13-*-*-*-*-*-iso10646-1",
        NULL
    };
    for (int i = 0; fonts[i] && !g_tray_font; i++) {
        int found = 0;
        char **names = XListFonts(g_tray_dpy, fonts[i], 1, &found);
        if (names && found > 0) {
            g_tray_font = XLoadFont(g_tray_dpy, names[0]);
            g_tray_font16 = 1;
        }
        if (names) XFreeFontNames(names);
    }
    if (!g_tray_font) {
        int found = 0;
        char **names = XListFonts(g_tray_dpy, "fixed", 1, &found);
        if (names && found > 0) {
            g_tray_font = XLoadFont(g_tray_dpy, names[0]);
            g_tray_font16 = 0;
        }
        if (names) XFreeFontNames(names);
    }
    if (g_tray_font) XSetFont(g_tray_dpy, g_tray_gc, g_tray_font);

    if (!g_tray_pix)
        g_tray_pix = tray_load_icon(icon_path, &g_tray_pix_w, &g_tray_pix_h);
    if (!tray_dock()) return 0;
    tray_apply_tooltip();
    tray_apply_menu();
    return 1;
}

static void tray_teardown(void) {
    if (g_tray_font) { XUnloadFont(g_tray_dpy, g_tray_font); g_tray_font = 0; }
    if (g_tray_gc) { XFreeGC(g_tray_dpy, g_tray_gc); g_tray_gc = NULL; }
    if (g_tray_win) { XDestroyWindow(g_tray_dpy, g_tray_win); g_tray_win = 0; }
    if (g_tray_dpy) { XCloseDisplay(g_tray_dpy); g_tray_dpy = NULL; }
    if (g_tray_pix) { free(g_tray_pix); g_tray_pix = NULL; }
    g_tray_pix_w = g_tray_pix_h = 0;
    g_tray_manager = 0;
    g_tray_item_n = 0;
}

static void *tray_thread_main(void *arg) {
    zan_tray_start_t *request = (zan_tray_start_t *)arg;
    char path[ZAN_TRAY_PATH_MAX];
    snprintf(path, sizeof(path), "%s", request->path);
    g_tray_pix = request->pixels;
    g_tray_pix_w = request->width;
    g_tray_pix_h = request->height;
    free(request);

    int ok = tray_setup(path);
    pthread_mutex_lock(&g_tray_mu);
    g_tray_start_rc = ok ? 1 : -1;
    pthread_cond_broadcast(&g_tray_cv);
    pthread_mutex_unlock(&g_tray_mu);
    if (!ok) {
        tray_teardown();
        pthread_mutex_lock(&g_tray_mu);
        g_tray_live = 0;
        pthread_mutex_unlock(&g_tray_mu);
        return NULL;
    }

    int xfd = ConnectionNumber(g_tray_dpy);
    int stop = 0;
    while (!stop) {
        struct pollfd fds[2];
        fds[0].fd = xfd;
        fds[0].events = POLLIN;
        fds[1].fd = g_tray_ctl[0];
        fds[1].events = POLLIN;
        /* 模块核心语义抽象与接口调用契约 */
        if (XPending(g_tray_dpy) == 0) poll(fds, 2, 1000);

        if (fds[1].revents & POLLIN) {
            char drain[64];
            while (read(g_tray_ctl[0], drain, sizeof(drain)) > 0) { }
        }
        pthread_mutex_lock(&g_tray_mu);
        int want_quit = g_tray_quit;
        int tip_dirty = g_tray_tip_dirty;
        int menu_dirty = g_tray_menu_dirty;
        pthread_mutex_unlock(&g_tray_mu);
        if (want_quit) break;
        if (tip_dirty) tray_apply_tooltip();
        if (menu_dirty) tray_apply_menu();

        while (XPending(g_tray_dpy)) {
            XEvent ev;
            XNextEvent(g_tray_dpy, &ev);
            switch (ev.type) {
            case Expose:
                tray_paint();
                break;
            case ConfigureNotify:
                if (ev.xconfigure.window == g_tray_win) {
                    if (ev.xconfigure.width > 0) g_tray_w = ev.xconfigure.width;
                    if (ev.xconfigure.height > 0) g_tray_h = ev.xconfigure.height;
                    tray_paint();
                }
                break;
            case ReparentNotify:
                tray_paint();
                break;
            case DestroyNotify:
                if (ev.xdestroywindow.window == g_tray_manager) {
                    g_tray_manager = 0;   /* 核心系统底层抽象与内存语义契约 */
                } else if (ev.xdestroywindow.window == g_tray_win) {
                    g_tray_win = 0;
                    stop = 1;
                }
                break;
            case ButtonPress:
                if (ev.xbutton.button == 3) {
                    tray_push(3);
                    int id = tray_show_menu();
                    if (id > 0) tray_push(0x10000 | id);
                } else if (ev.xbutton.button == 2) {
                    tray_push(4);
                } else if (ev.xbutton.button == 1) {
                    /* 内部辅助逻辑 */
                    tray_push(1);
                    if (g_tray_last_click != 0 &&
                        ev.xbutton.time - g_tray_last_click <= 400) {
                        tray_push(2);
                        g_tray_last_click = 0;
                    } else {
                        g_tray_last_click = ev.xbutton.time;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (!g_tray_manager && !stop) {
            if (tray_dock()) {
                tray_apply_tooltip();
                tray_paint();
            }
        }
    }

    tray_teardown();
    pthread_mutex_lock(&g_tray_mu);
    g_tray_live = 0;
    pthread_mutex_unlock(&g_tray_mu);
    tray_push(-1);
    return NULL;
}

static void tray_wake(void) {
    if (g_tray_ctl[1] >= 0) {
        char b = 1;
        ssize_t ignored = write(g_tray_ctl[1], &b, 1);
        (void)ignored;
    }
}

/* 核心系统底层抽象与内存语义契约 */

EXPORT i32 zan_tray_start_pixels(const zan_bitmap *bitmap,
                                  const char *icon_path, const char *tooltip) {
    zan_tray_start_t *request = (zan_tray_start_t *)malloc(sizeof(*request));
    if (!request) return 0;
    snprintf(request->path, sizeof(request->path), "%s", icon_path ? icon_path : "");
    request->pixels = tray_copy_bitmap(bitmap, &request->width, &request->height);

    pthread_mutex_lock(&g_tray_mu);
    if (g_tray_live || pipe(g_tray_ctl) != 0) {
        pthread_mutex_unlock(&g_tray_mu);
        free(request->pixels);
        free(request);
        return 0;
    }
    fcntl(g_tray_ctl[0], F_SETFL, O_NONBLOCK);
    snprintf(g_tray_tip, sizeof(g_tray_tip), "%s", tooltip ? tooltip : "");
    g_tray_menu_spec[0] = 0;
    g_tray_tip_dirty = 0;
    g_tray_menu_dirty = 0;
    g_tray_quit = 0;
    g_tray_start_rc = 0;
    g_tray_qhead = g_tray_qtail = 0;
    g_tray_live = 1;
    if (pthread_create(&g_tray_thread, NULL, tray_thread_main, request) != 0) {
        g_tray_live = 0;
        close(g_tray_ctl[0]);
        close(g_tray_ctl[1]);
        g_tray_ctl[0] = g_tray_ctl[1] = -1;
        pthread_mutex_unlock(&g_tray_mu);
        free(request->pixels);
        free(request);
        return 0;
    }
    while (g_tray_start_rc == 0) pthread_cond_wait(&g_tray_cv, &g_tray_mu);
    int rc = g_tray_start_rc;
    pthread_mutex_unlock(&g_tray_mu);
    if (rc != 1) {
        pthread_join(g_tray_thread, NULL);
        close(g_tray_ctl[0]);
        close(g_tray_ctl[1]);
        g_tray_ctl[0] = g_tray_ctl[1] = -1;
        return 0;
    }
    return 1;
}

/* 模块核心语义抽象与接口调用契约 */
EXPORT i32 zan_tray_start(const char *icon_path, const char *tooltip) {
    return zan_tray_start_pixels(NULL, icon_path, tooltip);
}

EXPORT i32 zan_tray_stop(void) {
    pthread_mutex_lock(&g_tray_mu);
    int live = g_tray_live;
    g_tray_quit = 1;
    pthread_mutex_unlock(&g_tray_mu);
    if (!live) return 0;
    tray_wake();
    pthread_join(g_tray_thread, NULL);
    if (g_tray_ctl[0] >= 0) { close(g_tray_ctl[0]); g_tray_ctl[0] = -1; }
    if (g_tray_ctl[1] >= 0) { close(g_tray_ctl[1]); g_tray_ctl[1] = -1; }
    return 1;
}

EXPORT i32 zan_tray_set_tooltip(const char *tooltip) {
    pthread_mutex_lock(&g_tray_mu);
    if (!g_tray_live) { pthread_mutex_unlock(&g_tray_mu); return 0; }
    snprintf(g_tray_tip, sizeof(g_tray_tip), "%s", tooltip ? tooltip : "");
    g_tray_tip_dirty = 1;
    pthread_mutex_unlock(&g_tray_mu);
    tray_wake();
    return 1;
}

EXPORT i32 zan_tray_set_menu(const char *packed) {
    pthread_mutex_lock(&g_tray_mu);
    if (!g_tray_live) { pthread_mutex_unlock(&g_tray_mu); return 0; }
    snprintf(g_tray_menu_spec, sizeof(g_tray_menu_spec), "%s",
             packed ? packed : "");
    g_tray_menu_dirty = 1;
    pthread_mutex_unlock(&g_tray_mu);
    tray_wake();
    return 1;
}

EXPORT i32 zan_tray_next_event(i32 timeout_ms) {
    pthread_mutex_lock(&g_tray_mu);
    if (g_tray_qhead == g_tray_qtail && timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_tray_cv, &g_tray_mu, &ts);
    }
    int ev = 0;
    if (g_tray_qhead != g_tray_qtail) {
        ev = g_tray_q[g_tray_qhead];
        g_tray_qhead = (g_tray_qhead + 1) % ZAN_TRAY_QCAP;
    }
    pthread_mutex_unlock(&g_tray_mu);
    return ev;
}

/* 核心系统底层抽象与内存语义契约 */

#define ZAN_LINUX_HOTKEY_QCAP 64
static int g_linux_hotkey_q[ZAN_LINUX_HOTKEY_QCAP];
static int g_linux_hotkey_qhead = 0;
static int g_linux_hotkey_qtail = 0;
static pthread_mutex_t g_linux_hotkey_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_linux_hotkey_cv = PTHREAD_COND_INITIALIZER;
static pthread_t       g_linux_hotkey_thread;
static int             g_linux_hotkey_live = 0;
static int             g_linux_hotkey_quit = 0;
static int             g_linux_hotkey_id = 0;
static int             g_linux_hotkey_mods = 0;
static int             g_linux_hotkey_vk = 0;
static int             g_linux_hotkey_start_rc = 0;
static Display        *g_linux_hotkey_dpy = NULL;

static void linux_hotkey_push(int ev) {
    pthread_mutex_lock(&g_linux_hotkey_mu);
    int next = (g_linux_hotkey_qtail + 1) % ZAN_LINUX_HOTKEY_QCAP;
    if (next != g_linux_hotkey_qhead) {
        g_linux_hotkey_q[g_linux_hotkey_qtail] = ev;
        g_linux_hotkey_qtail = next;
        pthread_cond_broadcast(&g_linux_hotkey_cv);
    }
    pthread_mutex_unlock(&g_linux_hotkey_mu);
}

static KeySym linux_vk_to_keysym(int vk) {
    if (vk >= 0x41 && vk <= 0x5A) return 'a' + (vk - 0x41);
    if (vk >= 0x30 && vk <= 0x39) return '0' + (vk - 0x30);
    if (vk >= 0x70 && vk <= 0x7B) return 0xFFBE + (vk - 0x70);  // XK_F1 = 0xFFBE
    if (vk == 0x0D) return 0xFF0D;  // XK_Return
    if (vk == 0x09) return 0xFF09;  // XK_Tab
    if (vk == 0x20) return 0x0020;  // XK_space
    if (vk == 0x08) return 0xFF08;  // XK_BackSpace
    if (vk == 0x1B) return 0xFF1B;  // XK_Escape
    if (vk == 0x25) return 0xFF51;  // XK_Left
    if (vk == 0x26) return 0xFF52;  // XK_Up
    if (vk == 0x27) return 0xFF53;  // XK_Right
    if (vk == 0x28) return 0xFF54;  // XK_Down
    return 0;
}

static unsigned int linux_mods_to_x11(int mods) {
    unsigned int xm = 0;
    if (mods & 0x0001) xm |= Mod1Mask;  // Alt
    if (mods & 0x0002) xm |= ControlMask;  // Ctrl
    if (mods & 0x0004) xm |= ShiftMask;  // Shift
    if (mods & 0x0008) xm |= Mod4Mask;  // Super / Win
    return xm;
}

static void *linux_hotkey_thread_fn(void *arg) {
    (void)arg;
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        pthread_mutex_lock(&g_linux_hotkey_mu);
        g_linux_hotkey_start_rc = -1;
        pthread_cond_broadcast(&g_linux_hotkey_cv);
        pthread_mutex_unlock(&g_linux_hotkey_mu);
        return NULL;
    }
    g_linux_hotkey_dpy = dpy;
    Window root = DefaultRootWindow(dpy);
    KeySym sym = linux_vk_to_keysym(g_linux_hotkey_vk);
    KeyCode code = sym ? XKeysymToKeycode(dpy, sym) : 0;
    if (!code) {
        XCloseDisplay(dpy);
        g_linux_hotkey_dpy = NULL;
        pthread_mutex_lock(&g_linux_hotkey_mu);
        g_linux_hotkey_start_rc = -1;
        pthread_cond_broadcast(&g_linux_hotkey_cv);
        pthread_mutex_unlock(&g_linux_hotkey_mu);
        return NULL;
    }

    unsigned int xm = linux_mods_to_x11(g_linux_hotkey_mods);
    unsigned int masks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    for (int i = 0; i < 4; i++) {
        XGrabKey(dpy, code, xm | masks[i], root, True, GrabModeAsync, GrabModeAsync);
    }
    XSelectInput(dpy, root, KeyPressMask);
    XFlush(dpy);

    pthread_mutex_lock(&g_linux_hotkey_mu);
    g_linux_hotkey_start_rc = 1;
    pthread_cond_broadcast(&g_linux_hotkey_cv);
    pthread_mutex_unlock(&g_linux_hotkey_mu);

    while (1) {
        pthread_mutex_lock(&g_linux_hotkey_mu);
        if (g_linux_hotkey_quit) {
            pthread_mutex_unlock(&g_linux_hotkey_mu);
            break;
        }
        pthread_mutex_unlock(&g_linux_hotkey_mu);

        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == KeyPress && ev.xkey.keycode == code) {
                linux_hotkey_push(g_linux_hotkey_id);
            }
        }
        struct timespec req = { 0, 10000000L }; /* 10ms */
        nanosleep(&req, NULL);
    }

    for (int i = 0; i < 4; i++) {
        XUngrabKey(dpy, code, xm | masks[i], root);
    }
    XCloseDisplay(dpy);
    g_linux_hotkey_dpy = NULL;
    return NULL;
}

EXPORT i32 zan_hotkey_register(i32 id, i32 mods, i32 vk) {
    pthread_mutex_lock(&g_linux_hotkey_mu);
    if (g_linux_hotkey_live) {
        pthread_mutex_unlock(&g_linux_hotkey_mu);
        return 0;
    }
    g_linux_hotkey_id = id;
    g_linux_hotkey_mods = mods;
    g_linux_hotkey_vk = vk;
    g_linux_hotkey_quit = 0;
    g_linux_hotkey_start_rc = 0;
    g_linux_hotkey_qhead = g_linux_hotkey_qtail = 0;

    if (pthread_create(&g_linux_hotkey_thread, NULL, linux_hotkey_thread_fn, NULL) != 0) {
        pthread_mutex_unlock(&g_linux_hotkey_mu);
        return 0;
    }
    g_linux_hotkey_live = 1;
    while (g_linux_hotkey_start_rc == 0) {
        pthread_cond_wait(&g_linux_hotkey_cv, &g_linux_hotkey_mu);
    }
    int ok = (g_linux_hotkey_start_rc == 1);
    if (!ok) g_linux_hotkey_live = 0;
    pthread_mutex_unlock(&g_linux_hotkey_mu);
    return ok ? 1 : 0;
}

EXPORT i32 zan_hotkey_unregister(i32 id) {
    (void)id;
    pthread_mutex_lock(&g_linux_hotkey_mu);
    if (!g_linux_hotkey_live) {
        pthread_mutex_unlock(&g_linux_hotkey_mu);
        return 0;
    }
    g_linux_hotkey_quit = 1;
    pthread_mutex_unlock(&g_linux_hotkey_mu);
    pthread_join(g_linux_hotkey_thread, NULL);
    pthread_mutex_lock(&g_linux_hotkey_mu);
    g_linux_hotkey_live = 0;
    pthread_mutex_unlock(&g_linux_hotkey_mu);
    linux_hotkey_push(-1);
    return 1;
}

EXPORT i32 zan_hotkey_next_event(i32 timeout_ms) {
    pthread_mutex_lock(&g_linux_hotkey_mu);
    if (g_linux_hotkey_qhead == g_linux_hotkey_qtail && timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_linux_hotkey_cv, &g_linux_hotkey_mu, &ts);
    }
    int ev = 0;
    if (g_linux_hotkey_qhead != g_linux_hotkey_qtail) {
        ev = g_linux_hotkey_q[g_linux_hotkey_qhead];
        g_linux_hotkey_qhead = (g_linux_hotkey_qhead + 1) % ZAN_LINUX_HOTKEY_QCAP;
    }
    pthread_mutex_unlock(&g_linux_hotkey_mu);
    return ev;
}

/* 底层系统交互与数据协议契约 */
EXPORT i32 zan_mouse_is_down(i32 button) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    Window root = DefaultRootWindow(dpy);
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask = 0;
    int down = 0;
    if (XQueryPointer(dpy, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
        if (button == 0) down = (mask & Button1Mask) ? 1 : 0;
        else if (button == 1) down = (mask & Button2Mask) ? 1 : 0;
        else if (button == 2) down = (mask & Button3Mask) ? 1 : 0;
        else if (button == 3) down = (mask & Button4Mask) ? 1 : 0;
        else if (button == 4) down = (mask & Button5Mask) ? 1 : 0;
    }
    XCloseDisplay(dpy);
    return down;
}

EXPORT i32 zan_keyboard_is_down(i32 vk) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    char keys[32];
    XQueryKeymap(dpy, keys);
    KeySym sym = (KeySym)linux_vk_to_keysym(vk);
    int down = 0;
    if (sym != NoSymbol) {
        KeyCode code = XKeysymToKeycode(dpy, sym);
        if (code != 0) {
            down = (keys[code / 8] & (1 << (code % 8))) ? 1 : 0;
        }
    }
    XCloseDisplay(dpy);
    return down;
}

EXPORT i32 zan_keyboard_is_toggled(i32 vk) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    XModifierKeymap *map = XGetModifierMapping(dpy);
    unsigned int mask = 0;
    Window root = DefaultRootWindow(dpy);
    Window root_ret, child_ret;
    int rx, ry, wx, wy;
    int toggled = 0;
    if (XQueryPointer(dpy, root, &root_ret, &child_ret, &rx, &ry, &wx, &wy, &mask)) {
        if (vk == 0x14 /* VK_CAPITAL */) {
            toggled = (mask & LockMask) ? 1 : 0;
        } else if (vk == 0x90 /* VK_NUMLOCK */) {
            if (map) {
                KeyCode num_code = XKeysymToKeycode(dpy, XK_Num_Lock);
                for (int i = 0; i < 8 * map->max_keypermod; i++) {
                    if (map->modifiermap[i] == num_code && num_code != 0) {
                        int mod_idx = i / map->max_keypermod;
                        unsigned int mod_mask = (1 << mod_idx);
                        if (mask & mod_mask) toggled = 1;
                        break;
                    }
                }
            }
        }
    }
    if (map) XFreeModifiermap(map);
    XCloseDisplay(dpy);
    return toggled;
}

EXPORT i32 zan_keyboard_post(i32 vk, i32 is_down) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    KeySym sym = (KeySym)linux_vk_to_keysym(vk);
    if (sym == NoSymbol) {
        XCloseDisplay(dpy);
        return 0;
    }
    KeyCode code = XKeysymToKeycode(dpy, sym);
    if (code == 0) {
        XCloseDisplay(dpy);
        return 0;
    }
    Window focus_win = 0;
    int revert_to = 0;
    XGetInputFocus(dpy, &focus_win, &revert_to);
    if (focus_win == 0 || focus_win == 1) {
        focus_win = DefaultRootWindow(dpy);
    }
    XKeyEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = is_down ? KeyPress : KeyRelease;
    evt.display = dpy;
    evt.window = focus_win;
    evt.root = DefaultRootWindow(dpy);
    evt.subwindow = None;
    evt.time = CurrentTime;
    evt.x = 1;
    evt.y = 1;
    evt.x_root = 1;
    evt.y_root = 1;
    evt.same_screen = True;
    evt.keycode = code;
    evt.state = 0;
    Status st = XSendEvent(dpy, focus_win, True, is_down ? KeyPressMask : KeyReleaseMask, (XEvent *)&evt);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return st != 0 ? 1 : 0;
}

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_LINUX_HOOK_QCAP 256
static pthread_mutex_t g_linux_hook_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_linux_hook_cv = PTHREAD_COND_INITIALIZER;
static i32 g_linux_hook_q[ZAN_LINUX_HOOK_QCAP * 5];
static i32 g_linux_hook_qhead = 0;
static i32 g_linux_hook_qtail = 0;
static i32 g_linux_hook_live = 0;

EXPORT i32 zan_hook_install(i32 kind) {
    pthread_mutex_lock(&g_linux_hook_mu);
    g_linux_hook_live = kind;
    g_linux_hook_qhead = g_linux_hook_qtail = 0;
    pthread_mutex_unlock(&g_linux_hook_mu);
    return 1;
}

EXPORT i32 zan_hook_uninstall(void) {
    pthread_mutex_lock(&g_linux_hook_mu);
    g_linux_hook_live = 0;
    pthread_mutex_unlock(&g_linux_hook_mu);
    return 1;
}

EXPORT i32 zan_hook_next_event(i32 timeout_ms, i32 *out_p1, i32 *out_p2, i32 *out_p3, i32 *out_p4, i32 *out_p5) {
    pthread_mutex_lock(&g_linux_hook_mu);
    if (g_linux_hook_qhead == g_linux_hook_qtail && timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_linux_hook_cv, &g_linux_hook_mu, &ts);
    }
    if (g_linux_hook_qhead != g_linux_hook_qtail) {
        if (out_p1) *out_p1 = g_linux_hook_q[g_linux_hook_qhead];
        if (out_p2) *out_p2 = g_linux_hook_q[g_linux_hook_qhead + 1];
        if (out_p3) *out_p3 = g_linux_hook_q[g_linux_hook_qhead + 2];
        if (out_p4) *out_p4 = g_linux_hook_q[g_linux_hook_qhead + 3];
        if (out_p5) *out_p5 = g_linux_hook_q[g_linux_hook_qhead + 4];
        g_linux_hook_qhead = (g_linux_hook_qhead + 5) % (ZAN_LINUX_HOOK_QCAP * 5);
        pthread_mutex_unlock(&g_linux_hook_mu);
        return 1;
    }
    pthread_mutex_unlock(&g_linux_hook_mu);
    return 0;
}

#elif !defined(_WIN32) && !defined(__APPLE__)

/* 核心系统底层抽象与内存语义契约 */
EXPORT i32 zan_tray_start(const char *icon_path, const char *tooltip) {
    (void)icon_path; (void)tooltip; return 0;
}
EXPORT i32 zan_tray_start_pixels(const zan_bitmap *bitmap,
                                  const char *icon_path, const char *tooltip) {
    (void)bitmap; (void)icon_path; (void)tooltip; return 0;
}
EXPORT i32 zan_tray_stop(void) { return 0; }
EXPORT i32 zan_tray_set_tooltip(const char *tooltip) { (void)tooltip; return 0; }
EXPORT i32 zan_tray_set_menu(const char *packed) { (void)packed; return 0; }
EXPORT i32 zan_tray_next_event(i32 timeout_ms) { (void)timeout_ms; return 0; }

EXPORT i32 zan_hotkey_register(i32 id, i32 mods, i32 vk) {
    (void)id; (void)mods; (void)vk; return 0;
}
EXPORT i32 zan_hotkey_unregister(i32 id) { (void)id; return 0; }
EXPORT i32 zan_hotkey_next_event(i32 timeout_ms) { (void)timeout_ms; return 0; }

EXPORT i32 zan_mouse_is_down(i32 button) { (void)button; return 0; }
EXPORT i32 zan_keyboard_is_down(i32 vk) { (void)vk; return 0; }
EXPORT i32 zan_keyboard_is_toggled(i32 vk) { (void)vk; return 0; }
EXPORT i32 zan_keyboard_post(i32 vk, i32 is_down) { (void)vk; (void)is_down; return 0; }
EXPORT i32 zan_hook_install(i32 kind) { (void)kind; return 0; }
EXPORT i32 zan_hook_uninstall(void) { return 0; }
EXPORT i32 zan_hook_next_event(i32 timeout_ms, i32 *out_p1, i32 *out_p2, i32 *out_p3, i32 *out_p4, i32 *out_p5) {
    (void)timeout_ms; (void)out_p1; (void)out_p2; (void)out_p3; (void)out_p4; (void)out_p5;
    return 0;
}

#endif
