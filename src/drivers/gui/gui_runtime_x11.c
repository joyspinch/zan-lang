/* gui_runtime_x11 */


#if defined(__linux__)

#include <dlfcn.h>

static Display *g_display = NULL;
static Window g_x11_window = 0;
static int g_wake_pipe[2] = { -1, -1 };
static int g_pending_event_linux[8];
static int g_win_w = 0, g_win_h = 0;
static char *g_clip_text_linux = NULL;
static char *g_clip_read_linux = NULL;
static Cursor g_cursors_linux[8];
static XIM g_xim = NULL;
static XIC g_xic = NULL;
static XFontSet g_ime_fontset_linux = NULL;

static int g_scale_linux = 100;
static int g_titlebar_h_l = 32;
static int g_btn_w_l = 46;
static int g_caption_btn_count_l = 5;
static int g_metrics_ready_linux = 0;

#define ZAN_MAX_WINDOWS 256
typedef struct {
    Window xid;
    GC gc;
    XIC xic;
    int ime_position;
    Pixmap backbuf;
    int backbuf_w, backbuf_h;
    int w, h;
    struct {
        Window source, requestor;
        i64 deadline;
        int offered, accepted, pending, x, y;
    } dnd;
} zan_lwin_t;
static zan_lwin_t g_lwins[ZAN_MAX_WINDOWS];
static int g_lwin_count = 0;
static Window g_primary_win = 0;
static Window g_evwin_linux = 0;

static zan_lwin_t *lwin_find(Window xid) {
    for (int i = 0; i < g_lwin_count; i++)
        if (g_lwins[i].xid == xid) return &g_lwins[i];
    return NULL;
}

i32 zan_gui_get_dpi_scale(void);
i32 zan_gui_is_maximized(iptr hwnd_val);
i64 zan_gui_get_tick_ms(void);

static void x11_set_scale_metrics(int scale) {
    if (scale <= 0) { scale = 100; }
    if (g_metrics_ready_linux && g_scale_linux == scale) { return; }
    g_scale_linux = scale;
    g_titlebar_h_l = 32 * scale / 100;
    g_btn_w_l = 46 * scale / 100;
    g_metrics_ready_linux = 1;
}

#define ZAN_EVQ_CAP 64
static int g_evq_linux[ZAN_EVQ_CAP][8];
static int g_evq_head_linux = 0, g_evq_tail_linux = 0;

static long long g_ev_seq_linux = 0;

static void evq_push_linux(int kind, int x, int y, int button, int keycode, int mods) {
    int next = (g_evq_tail_linux + 1) % ZAN_EVQ_CAP;
    if (next == g_evq_head_linux) return;
    int *e = g_evq_linux[g_evq_tail_linux];
    e[0] = kind; e[1] = x; e[2] = y; e[3] = button;
    e[4] = keycode; e[5] = mods; e[6] = (int)g_evwin_linux; e[7] = 0;
    g_evq_tail_linux = next;
}

static void x11_drop_activate(int slot);

static int evq_pop_linux(void) {
    if (g_evq_head_linux == g_evq_tail_linux) return 0;
    x11_drop_activate(g_evq_head_linux);
    memcpy(g_pending_event_linux, g_evq_linux[g_evq_head_linux],
           sizeof(g_pending_event_linux));
    g_evq_head_linux = (g_evq_head_linux + 1) % ZAN_EVQ_CAP;
    g_ev_seq_linux++;
    return 1;
}

static unsigned x11_utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned cp = *s;
    if (cp < 0x80) { *p += 1; return cp; }
    else if ((cp >> 5) == 0x6) { cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F); *p += 2; }
    else if ((cp >> 4) == 0xE) { cp = ((cp & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else if ((cp >> 3) == 0x1E) { cp = ((cp & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *p += 4; }
    else { *p += 1; }
    return cp;
}

static int x11_mods(unsigned int state) {
    int m = 0;
    if (state & ControlMask) m |= 1;
    if (state & ShiftMask)   m |= 2;
    if (state & Mod1Mask)    m |= 4;
    return m;
}

static int x11_vk_from_keysym(KeySym ks) {
    switch (ks) {
    case XK_Escape:    return 27;
    case XK_Return:
    case XK_KP_Enter:  return 13;
    case XK_Tab:       return 9;
    case XK_BackSpace: return 8;
    case XK_Delete:    return 46;
    case XK_Left:      return 37;
    case XK_Up:        return 38;
    case XK_Right:     return 39;
    case XK_Down:      return 40;
    case XK_Home:      return 36;
    case XK_End:       return 35;
    case XK_Prior:     return 33; /* PageUp */
    case XK_Next:      return 34; /* PageDown */
    case XK_space:     return 32;
    case XK_F1:        return 112;
    case XK_F2:        return 113;
    case XK_F5:        return 116;
    case XK_F11:       return 122;
    case XK_Shift_L:
    case XK_Shift_R:   return 16;
    case XK_Control_L:
    case XK_Control_R: return 17;
    case XK_Alt_L:
    case XK_Alt_R:     return 18;
    default: break;
    }
    if (ks >= XK_a && ks <= XK_z) return (int)(ks - XK_a + 'A');
    if ((ks >= XK_A && ks <= XK_Z) || (ks >= XK_0 && ks <= XK_9)) return (int)ks;
    return (int)ks;
}

static Atom x11_atom(const char *name) { return XInternAtom(g_display, name, False); }

#include "gui_runtime_x11_drop.c"

static void x11_wm_state(Window win, Atom state1, Atom state2, long action) {
    if (!g_display || !win) return;
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = x11_atom("_NET_WM_STATE");
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = action;
    xev.xclient.data.l[1] = (long)state1;
    xev.xclient.data.l[2] = (long)state2;
    xev.xclient.data.l[3] = 1;
    XSendEvent(g_display, DefaultRootWindow(g_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(g_display);
}

#define ZAN_NWMR_TOPLEFT     0
#define ZAN_NWMR_TOP         1
#define ZAN_NWMR_TOPRIGHT    2
#define ZAN_NWMR_RIGHT       3
#define ZAN_NWMR_BOTTOMRIGHT 4
#define ZAN_NWMR_BOTTOM      5
#define ZAN_NWMR_BOTTOMLEFT  6
#define ZAN_NWMR_LEFT        7
#define ZAN_NWMR_MOVE        8

static void x11_set_borderless(Window win) {
    if (!g_display || !win) return;
    struct {
        unsigned long flags, functions, decorations;
        long input_mode;
        unsigned long status;
    } hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = (1L << 1); /* MWM_HINTS_DECORATIONS */
    hints.decorations = 0;
    Atom a = x11_atom("_MOTIF_WM_HINTS");
    XChangeProperty(g_display, win, a, a, 32, PropModeReplace,
                    (unsigned char *)&hints, 5);
}

static void x11_start_moveresize(Window win, int x_root, int y_root, int direction) {
    if (!g_display || !win) return;
    XUngrabPointer(g_display, CurrentTime);
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = x11_atom("_NET_WM_MOVERESIZE");
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = x_root;
    xev.xclient.data.l[1] = y_root;
    xev.xclient.data.l[2] = direction;
    xev.xclient.data.l[3] = 1; /* button 1 */
    xev.xclient.data.l[4] = 1;
    XSendEvent(g_display, DefaultRootWindow(g_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(g_display);
}

static int x11_caption_hit(zan_lwin_t *lw, int x, int y) {
    int w = lw->w, h = lw->h;
    int capW = g_caption_btn_count_l * g_btn_w_l;
    int inCaptionDrag = (y < g_titlebar_h_l && x < w - capW);
    if (zan_gui_is_maximized((iptr)lw->xid)) {
        return inCaptionDrag ? ZAN_NWMR_MOVE : -1;
    }
    int b = 8 * g_scale_linux / 100;
    if (b < 4) b = 4;
    int left = x < b, right = x >= w - b, top = y < b, bottom = y >= h - b;
    if (top && left) return ZAN_NWMR_TOPLEFT;
    if (top && right) return ZAN_NWMR_TOPRIGHT;
    if (bottom && left) return ZAN_NWMR_BOTTOMLEFT;
    if (bottom && right) return ZAN_NWMR_BOTTOMRIGHT;
    if (!zan_gui_in_hit_guard((iptr)lw->xid, x, y)) {
        if (left) return ZAN_NWMR_LEFT;
        if (right) return ZAN_NWMR_RIGHT;
        if (top) return ZAN_NWMR_TOP;
        if (bottom) return ZAN_NWMR_BOTTOM;
    }
    if (inCaptionDrag) return ZAN_NWMR_MOVE;
    return -1;
}

static void x11_serve_selection(XSelectionRequestEvent *req) {
    XSelectionEvent resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = SelectionNotify;
    resp.display = req->display;
    resp.requestor = req->requestor;
    resp.selection = req->selection;
    resp.target = req->target;
    resp.time = req->time;
    resp.property = req->property ? req->property : req->target;

    Atom a_utf8 = x11_atom("UTF8_STRING");
    Atom a_targets = x11_atom("TARGETS");
    const char *txt = g_clip_text_linux ? g_clip_text_linux : "";

    if (req->target == a_targets) {
        Atom offered[3] = { a_targets, a_utf8, XA_STRING };
        XChangeProperty(g_display, req->requestor, resp.property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)offered, 3);
    } else if (req->target == a_utf8 || req->target == XA_STRING) {
        XChangeProperty(g_display, req->requestor, resp.property, req->target, 8,
                        PropModeReplace, (const unsigned char *)txt, (int)strlen(txt));
    } else {
        resp.property = None;
    }
    XSendEvent(g_display, req->requestor, False, 0, (XEvent *)&resp);
    XFlush(g_display);
}

/* X11 buttons: 1=left 2=middle 3=right */
static int x11_abi_button(unsigned int b) {
    if (b == 3) return 1;
    if (b == 2) return 2;
    return (int)b - 1;
}

static void x11_translate_event(XEvent *ev) {
    g_evwin_linux = ev->xany.window;
    switch (ev->type) {
    case MotionNotify:
        evq_push_linux(1, ev->xmotion.x, ev->xmotion.y, 0, 0,
                       x11_mods(ev->xmotion.state));
        break;
    case ButtonPress:
        if (ev->xbutton.button == 4 || ev->xbutton.button == 5) {
            int delta = ev->xbutton.button == 4 ? 120 : -120;
            evq_push_linux(13, ev->xbutton.x, ev->xbutton.y, 0, delta,
                           x11_mods(ev->xbutton.state));
        } else if (ev->xbutton.button == 6 || ev->xbutton.button == 7) {
        } else if (ev->xbutton.button == 1) {
            zan_lwin_t *bw = lwin_find(ev->xbutton.window);
            int dir = bw ? x11_caption_hit(bw, ev->xbutton.x, ev->xbutton.y) : -1;
            if (dir >= 0) {
                x11_start_moveresize(ev->xbutton.window,
                                     ev->xbutton.x_root, ev->xbutton.y_root, dir);
            } else {
                evq_push_linux(2, ev->xbutton.x, ev->xbutton.y, 0, 0,
                               x11_mods(ev->xbutton.state));
            }
        } else {
            evq_push_linux(2, ev->xbutton.x, ev->xbutton.y,
                           x11_abi_button(ev->xbutton.button), 0,
                           x11_mods(ev->xbutton.state));
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button >= 4 && ev->xbutton.button <= 7) break;
        evq_push_linux(3, ev->xbutton.x, ev->xbutton.y,
                       x11_abi_button(ev->xbutton.button), 0,
                       x11_mods(ev->xbutton.state));
        break;
    case KeyPress: {
        int mods = x11_mods(ev->xkey.state);
        KeySym ks = 0;
        char local[64];
        char *buf = local;
        int n;
        zan_lwin_t *kw = lwin_find(ev->xkey.window);
        XIC xic = kw ? kw->xic : NULL;
        if (xic) {
            Status st = 0;
            int capacity = (int)sizeof(local) - 1;
            n = Xutf8LookupString(xic, &ev->xkey, buf, capacity, &ks, &st);
            if (st == XBufferOverflow) {
                if (n <= 0 || n > 1024 * 1024) break;
                buf = (char *)malloc((size_t)n + 1);
                if (!buf) break;
                capacity = n;
                n = Xutf8LookupString(xic, &ev->xkey, buf, capacity, &ks, &st);
            }
            if (n < 0 || n > capacity || (st != XLookupChars && st != XLookupBoth)) n = 0;
        } else {
            n = XLookupString(&ev->xkey, buf, sizeof(local) - 1, &ks, NULL);
        }
        evq_push_linux(4, 0, 0, 0, x11_vk_from_keysym(ks), mods);
        if (n > 0) {
            buf[n] = '\0';
            if (!x11_utf8_valid((const unsigned char *)buf)) n = 0;
        }
        if (n > 0) {
            const char *p = buf;
            while (*p) {
                unsigned cp = x11_utf8_next(&p);
                if (cp != 0 && cp != 127)
                    evq_push_linux(6, 0, 0, 0, (int)cp, mods);
            }
        }
        if (buf != local) free(buf);
        break;
    }
    case KeyRelease: {
        KeySym ks = XLookupKeysym(&ev->xkey, 0);
        evq_push_linux(5, 0, 0, 0, x11_vk_from_keysym(ks),
                       x11_mods(ev->xkey.state));
        break;
    }
    case ConfigureNotify: {
        zan_lwin_t *cw = lwin_find(ev->xconfigure.window);
        if (cw && (ev->xconfigure.width != cw->w ||
                   ev->xconfigure.height != cw->h)) {
            cw->w = ev->xconfigure.width;
            cw->h = ev->xconfigure.height;
            if (ev->xconfigure.window == g_primary_win) {
                g_win_w = cw->w;
                g_win_h = cw->h;
            }
            evq_push_linux(7, cw->w, cw->h, 0, 0, 0);
        }
        break;
    }
    case Expose: {
        zan_lwin_t *ew = lwin_find(ev->xexpose.window);
        if (ew && ew->backbuf)
            XCopyArea(g_display, ew->backbuf, ew->xid, ew->gc, 0, 0,
                      (unsigned)ew->backbuf_w, (unsigned)ew->backbuf_h, 0, 0);
        break;
    }
    case SelectionNotify:
        x11_dnd_selection(&ev->xselection);
        break;
    case ClientMessage:
        if (x11_dnd_client(&ev->xclient)) break;
        if (ev->xclient.message_type == x11_atom("WM_PROTOCOLS") &&
            ev->xclient.format == 32 &&
            (Atom)ev->xclient.data.l[0] == x11_atom("WM_DELETE_WINDOW"))
            evq_push_linux(8, 0, 0, 0, 0, 0);
        break;
    case FocusIn: {
        zan_lwin_t *fw = lwin_find(ev->xfocus.window);
        if (fw && fw->xic) XSetICFocus(fw->xic);
        break;
    }
    case FocusOut: {
        zan_lwin_t *fw = lwin_find(ev->xfocus.window);
        if (fw && fw->xic) XUnsetICFocus(fw->xic);
        evq_push_linux(9, 0, 0, 0, 0, 0);
        break;
    }
    }
}

static XIC x11_create_ic(Window xid, int *position) {
    *position = 0;
    if (!g_xim) return NULL;
    XIMStyles *styles = NULL;
    if (XGetIMValues(g_xim, XNQueryInputStyle, &styles, NULL) != NULL || !styles)
        return NULL;
    const XIMStyle preferred[] = {
        XIMPreeditPosition | XIMStatusNothing,
        XIMPreeditNothing | XIMStatusNothing,
        XIMPreeditNone | XIMStatusNone
    };
    XIC xic = NULL;
    for (unsigned p = 0; p < sizeof(preferred) / sizeof(preferred[0]) && !xic; p++) {
        for (unsigned short i = 0; i < styles->count_styles && !xic; i++) {
            if (styles->supported_styles[i] != preferred[p]) continue;
            if (preferred[p] & XIMPreeditPosition) {
                if (!g_ime_fontset_linux) {
                    char **missing = NULL;
                    int missing_count = 0;
                    char *default_string = NULL;
                    g_ime_fontset_linux = XCreateFontSet(g_display, "fixed",
                        &missing, &missing_count, &default_string);
                    if (missing) XFreeStringList(missing);
                }
                if (!g_ime_fontset_linux) continue;
                XPoint spot = { 0, 0 };
                XVaNestedList attrs = XVaCreateNestedList(0, XNSpotLocation, &spot,
                    XNFontSet, g_ime_fontset_linux, NULL);
                if (!attrs) continue;
                xic = XCreateIC(g_xim, XNInputStyle, preferred[p],
                    XNClientWindow, xid, XNFocusWindow, xid,
                    XNPreeditAttributes, attrs, NULL);
                XFree(attrs);
                if (xic) *position = 1;
            } else {
                xic = XCreateIC(g_xim, XNInputStyle, preferred[p],
                    XNClientWindow, xid, XNFocusWindow, xid, NULL);
            }
        }
    }
    XFree(styles);
    return xic;
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    if (!g_display) {
        g_display = XOpenDisplay(NULL);
        if (!g_display) return 0;
    }
    zan_gui_get_dpi_scale();
    if (!g_xim) {
        setlocale(LC_ALL, "");
        XSetLocaleModifiers("");
        g_xim = XOpenIM(g_display, NULL, NULL, NULL);
    }
    if (g_wake_pipe[0] < 0 && pipe(g_wake_pipe) == 0) {
        fcntl(g_wake_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(g_wake_pipe[1], F_SETFL, O_NONBLOCK);
        fcntl(g_wake_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(g_wake_pipe[1], F_SETFD, FD_CLOEXEC);
    }
    if (g_lwin_count >= ZAN_MAX_WINDOWS) return 0;

    int screen = DefaultScreen(g_display);
    Window xid = XCreateSimpleWindow(g_display, RootWindow(g_display, screen),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(g_display, screen), WhitePixel(g_display, screen));

    XStoreName(g_display, xid, title);
    long event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        StructureNotifyMask | FocusChangeMask;
    XSelectInput(g_display, xid, event_mask);

    Atom wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, xid, &wm_delete, 1);
    unsigned long dnd_version = 5;
    XChangeProperty(g_display, xid, x11_atom("XdndAware"), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&dnd_version, 1);

    int ime_position = 0;
    XIC xic = x11_create_ic(xid, &ime_position);
    long filter_events = 0;
    if (xic && XGetICValues(xic, XNFilterEvents, &filter_events, NULL) == NULL)
        XSelectInput(g_display, xid, event_mask | filter_events);
    GC gc = XCreateGC(g_display, xid, 0, NULL);

    zan_lwin_t *w = &g_lwins[g_lwin_count++];
    memset(w, 0, sizeof(*w));
    w->xid = xid;
    w->gc = gc;
    w->xic = xic;
    w->ime_position = ime_position;
    w->backbuf = 0;
    w->backbuf_w = 0;
    w->backbuf_h = 0;
    w->w = width;
    w->h = height;

    if (!g_primary_win) {
        g_primary_win = xid;
        g_x11_window = xid;
        g_xic = xic;
        g_win_w = width;
        g_win_h = height;
    } else {
        XWindowAttributes parent_attr;
        Window child = 0;
        int parent_x = 0;
        int parent_y = 0;
        Window root = RootWindow(g_display, screen);
        int screen_w = DisplayWidth(g_display, screen);
        int screen_h = DisplayHeight(g_display, screen);
        if (XGetWindowAttributes(g_display, g_primary_win, &parent_attr) &&
            XTranslateCoordinates(g_display, g_primary_win, root, 0, 0,
                                  &parent_x, &parent_y, &child)) {
            int x = parent_x + (parent_attr.width - width) / 2;
            int y = parent_y + (parent_attr.height - height) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + width > screen_w) x = screen_w - width;
            if (y + height > screen_h) y = screen_h - height;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            XMoveWindow(g_display, xid, x, y);
        }
    }
    x11_set_borderless(xid);

    return (i64)xid;
}

EXPORT i32 zan_gui_show_window(iptr win) {
    Window xid = win ? (Window)(intptr_t)win : g_x11_window;
    if (g_display && xid) {
        XMapWindow(g_display, xid);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i32 zan_gui_wait_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    x11_dnd_poll_timeout(0);
    if (evq_pop_linux()) return 0;

    int xfd = ConnectionNumber(g_display);
    XEvent ev;
    for (;;) {
        while (XPending(g_display) > 0) {
            x11_dnd_poll_timeout(0);
            XNextEvent(g_display, &ev);
            if (ev.type == SelectionRequest) {
                x11_serve_selection(&ev.xselectionrequest);
                continue;
            }
            if (XFilterEvent(&ev, None)) continue;
            x11_translate_event(&ev);
            if (evq_pop_linux()) return 0;
        }
        struct pollfd fds[2];
        fds[0].fd = xfd; fds[0].events = POLLIN; fds[0].revents = 0;
        int nfds = 1;
        if (g_wake_pipe[0] >= 0) {
            fds[1].fd = g_wake_pipe[0]; fds[1].events = POLLIN; fds[1].revents = 0;
            nfds = 2;
        }
        int poll_ms = x11_dnd_poll_timeout(-1);
        if (XPending(g_display) > 0) continue;
        int pr = poll(fds, (nfds_t)nfds, poll_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        x11_dnd_poll_timeout(0);
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            char buf[64];
            while (read(g_wake_pipe[0], buf, sizeof(buf)) > 0) { }
            /* 返回a benign empty frame (kind 0) so the caller drains its dispatch queue */
            return 0;
        }
    }
}

EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    x11_dnd_poll_timeout(0);
    if (evq_pop_linux()) return 0;

    int xfd = ConnectionNumber(g_display);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    XEvent ev;
    for (;;) {
        while (XPending(g_display) > 0) {
            x11_dnd_poll_timeout(0);
            XNextEvent(g_display, &ev);
            if (ev.type == SelectionRequest) {
                x11_serve_selection(&ev.xselectionrequest);
                continue;
            }
            if (XFilterEvent(&ev, None)) continue;
            x11_translate_event(&ev);
            if (evq_pop_linux()) return 0;
        }
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        long elapsed = (tn.tv_sec - t0.tv_sec) * 1000
                     + (tn.tv_nsec - t0.tv_nsec) / 1000000;
        long remain = (long)ms - elapsed;
        if (remain <= 0) return 1;
        struct pollfd fds[2];
        fds[0].fd = xfd; fds[0].events = POLLIN; fds[0].revents = 0;
        int nfds = 1;
        if (g_wake_pipe[0] >= 0) {
            fds[1].fd = g_wake_pipe[0]; fds[1].events = POLLIN; fds[1].revents = 0;
            nfds = 2;
        }
        int poll_ms = x11_dnd_poll_timeout((int)remain);
        if (XPending(g_display) > 0) continue;
        int pr = poll(fds, (nfds_t)nfds, poll_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        x11_dnd_poll_timeout(0);
        if (pr == 0) continue;
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            char buf[64];
            while (read(g_wake_pipe[0], buf, sizeof(buf)) > 0) { }
            return 0;
        }
    }
}

EXPORT i32 zan_gui_wake(void) {
    if (g_wake_pipe[1] >= 0) {
        char b = 1;
        ssize_t n = write(g_wake_pipe[1], &b, 1);
        (void)n;
    }
    return 0;
}

EXPORT i32 zan_gui_poll_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    x11_dnd_poll_timeout(0);
    if (evq_pop_linux()) return 0;
    for (;;) {
        x11_dnd_poll_timeout(0);
        if (XPending(g_display) <= 0) return 1;
        XEvent ev;
        XNextEvent(g_display, &ev);
        if (ev.type == SelectionRequest) {
            x11_serve_selection(&ev.xselectionrequest);
            continue;
        }
        if (XFilterEvent(&ev, None)) continue;
        x11_translate_event(&ev);
        if (evq_pop_linux()) return 0;
    }
}

EXPORT i32 zan_gui_inject_event(
    iptr hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    Window previous = g_evwin_linux;
    g_evwin_linux = hwnd_val ? (Window)(uintptr_t)hwnd_val : g_primary_win;
    evq_push_linux((int)kind, (int)x, (int)y, (int)button, (int)keycode, (int)mods);
    g_evwin_linux = previous;
    zan_gui_wake();
    return 0;
}

EXPORT i32 zan_gui_inject_pending(void) {
    return (g_evq_tail_linux - g_evq_head_linux + ZAN_EVQ_CAP) % ZAN_EVQ_CAP;
}

EXPORT i32 zan_gui_event_kind(void)    { return g_pending_event_linux[0]; }
EXPORT i64 zan_gui_event_seq(void)     { return g_ev_seq_linux; }
EXPORT i32 zan_gui_event_x(void)       { return g_pending_event_linux[1]; }
EXPORT i32 zan_gui_event_y(void)       { return g_pending_event_linux[2]; }
EXPORT i32 zan_gui_event_button(void)  { return g_pending_event_linux[3]; }
EXPORT i32 zan_gui_event_keycode(void) { return g_pending_event_linux[4]; }
EXPORT i32 zan_gui_event_mods(void)    { return g_pending_event_linux[5]; }
EXPORT i32 zan_gui_event_flag(void)    { return 0; }

EXPORT i32 zan_gui_window_width(void)  { return g_win_w; }
EXPORT i32 zan_gui_window_height(void) { return g_win_h; }

EXPORT iptr zan_gui_event_hwnd(void)    { return (iptr)(unsigned)g_pending_event_linux[6]; }
EXPORT int zan_gui_resize_client(iptr hwnd_val, i32 width, i32 height) {
    if (!g_display || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return -1;
    zan_lwin_t *w = lwin_find((Window)(uintptr_t)hwnd_val);
    if (!w) return -1;
    XResizeWindow(g_display, w->xid, (unsigned)width, (unsigned)height);
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_client_width(iptr hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->w : g_win_w;
}
EXPORT i32 zan_gui_client_height(iptr hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->h : g_win_h;
}

#define ZAN_DIRTY_MAX 512
static int g_dirty[ZAN_DIRTY_MAX * 4];
static int g_dirty_count;
static int g_dirty_overflow;

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_dirty_count >= ZAN_DIRTY_MAX) { g_dirty_overflow = 1; return 0; }
    g_dirty[g_dirty_count * 4 + 0] = (int)x;
    g_dirty[g_dirty_count * 4 + 1] = (int)y;
    g_dirty[g_dirty_count * 4 + 2] = (int)w;
    g_dirty[g_dirty_count * 4 + 3] = (int)h;
    g_dirty_count++;
    return 0;
}

EXPORT void zan_gui_present_full(void) {
    g_dirty_count = 0;
    g_dirty_overflow = 0;
}

EXPORT i32 zan_gui_present(iptr hwnd_val, i32 surface_id) {
    if (!g_display) return 1;
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    if (!w && g_lwin_count > 0) w = &g_lwins[0];
    if (!w) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    if (zan_gui_present_window(surface_id, (void *)(intptr_t)w->xid)) {
        g_dirty_count = 0;
        g_dirty_overflow = 0;
        return 0;
    }
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }

    int screen = DefaultScreen(g_display);
    unsigned depth = (unsigned)DefaultDepth(g_display, screen);

    if (!w->backbuf || w->backbuf_w != s->width || w->backbuf_h != s->height) {
        g_dirty_count = 0;
        g_dirty_overflow = 1;
        if (w->backbuf) XFreePixmap(g_display, w->backbuf);
        w->backbuf = XCreatePixmap(g_display, w->xid,
                                   (unsigned)s->width, (unsigned)s->height, depth);
        w->backbuf_w = s->width;
        w->backbuf_h = s->height;
    }

    XImage *img = XCreateImage(g_display, DefaultVisual(g_display, screen),
        depth, ZPixmap, 0, (char *)s->pixels, (unsigned)s->width, (unsigned)s->height, 32, 0);
    if (img) {
        img->byte_order = LSBFirst;
        if (g_dirty_count > 0 && !g_dirty_overflow) {
            for (int i = 0; i < g_dirty_count; i++) {
                int rx = g_dirty[i * 4 + 0];
                int ry = g_dirty[i * 4 + 1];
                int rw = g_dirty[i * 4 + 2];
                int rh = g_dirty[i * 4 + 3];
                if (rx < 0) { rw += rx; rx = 0; }
                if (ry < 0) { rh += ry; ry = 0; }
                if (rx + rw > s->width)  rw = s->width - rx;
                if (ry + rh > s->height) rh = s->height - ry;
                if (rw <= 0 || rh <= 0) continue;
                XPutImage(g_display, w->backbuf, w->gc, img, rx, ry, rx, ry,
                          (unsigned)rw, (unsigned)rh);
                XCopyArea(g_display, w->backbuf, w->xid, w->gc, rx, ry,
                          (unsigned)rw, (unsigned)rh, rx, ry);
            }
        } else {
            XPutImage(g_display, w->backbuf, w->gc, img, 0, 0, 0, 0,
                      (unsigned)s->width, (unsigned)s->height);
            XCopyArea(g_display, w->backbuf, w->xid, w->gc, 0, 0,
                      (unsigned)s->width, (unsigned)s->height, 0, 0);
        }
        img->data = NULL;
        XDestroyImage(img);
    }
    g_dirty_count = 0;
    g_dirty_overflow = 0;
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_set_title(iptr hwnd_val, const char *title) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XStoreName(g_display, xid, title);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i32 zan_gui_set_window_pos(iptr hwnd_val, i32 x, i32 y) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 1;
    XMoveWindow(g_display, xid, (int)x, (int)y);
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_center_window(iptr hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 1;
    Window root; int wx = 0, wy = 0;
    unsigned int ww = 0, wh = 0, bw = 0, depth = 0;
    if (!XGetGeometry(g_display, xid, &root, &wx, &wy, &ww, &wh, &bw, &depth))
        return 1;
    int screen = DefaultScreen(g_display);
    int sw = DisplayWidth(g_display, screen);
    int sh = DisplayHeight(g_display, screen);
    int nx = (sw - (int)ww) / 2;
    int ny = (sh - (int)wh) / 2;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    XMoveWindow(g_display, xid, nx, ny);
    XFlush(g_display);
    return 0;
}

EXPORT i32 zan_gui_set_cursor(i32 cursor_type) {
    if (!g_display || g_lwin_count == 0) return 1;
    int slot = (int)cursor_type;
    if (slot < 0 || slot >= 8) slot = 0;
    if (!g_cursors_linux[slot]) {
        unsigned int shape = XC_left_ptr;
        if (slot == 1) shape = XC_hand2;
        else if (slot == 2) shape = XC_xterm;
        else if (slot == 3) shape = XC_sb_h_double_arrow;
        else if (slot == 4) shape = XC_sb_v_double_arrow;
        else if (slot == 5) shape = XC_crosshair;
        g_cursors_linux[slot] = XCreateFontCursor(g_display, shape);
    }
    for (int i = 0; i < g_lwin_count; i++)
        XDefineCursor(g_display, g_lwins[i].xid, g_cursors_linux[slot]);
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

EXPORT void zan_gui_sleep_ms(i32 ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&req, NULL);
}

#if defined(__GNUC__)
__attribute__((weak)) int issetugid(void) {
    return (getuid() != geteuid() || getgid() != getegid()) ? 1 : 0;
}
#endif

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

typedef void *(*zan_xrr_get_screen_info_fn)(Display *, Window);
typedef short (*zan_xrr_current_rate_fn)(void *);
typedef void (*zan_xrr_free_screen_config_fn)(void *);

static int g_xrr_resolved = 0;
static zan_xrr_get_screen_info_fn g_xrr_get_screen_info = NULL;
static zan_xrr_current_rate_fn g_xrr_current_rate = NULL;
static zan_xrr_free_screen_config_fn g_xrr_free_screen_config = NULL;
static double g_xrr_cached_hz = 60.0;
static long long g_xrr_cached_ns = 0;

EXPORT double zan_gui_refresh_rate(iptr hwnd_val) {
    if (!g_display) return 60.0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now_ns = (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
    if (g_xrr_cached_ns > 0 && now_ns - g_xrr_cached_ns < 1000000000LL) {
        return g_xrr_cached_hz;
    }
    if (!g_xrr_resolved) {
        g_xrr_resolved = 1;
        void *lib = dlopen("libXrandr.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) lib = dlopen("libXrandr.so", RTLD_LAZY | RTLD_LOCAL);
        if (lib) {
            g_xrr_get_screen_info = (zan_xrr_get_screen_info_fn)dlsym(lib, "XRRGetScreenInfo");
            g_xrr_current_rate = (zan_xrr_current_rate_fn)dlsym(lib, "XRRConfigCurrentRate");
            g_xrr_free_screen_config = (zan_xrr_free_screen_config_fn)dlsym(lib, "XRRFreeScreenConfigInfo");
        }
    }
    double hz = 60.0;
    if (g_xrr_get_screen_info && g_xrr_current_rate && g_xrr_free_screen_config) {
        Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
        if (!xid) xid = DefaultRootWindow(g_display);
        void *cfg = g_xrr_get_screen_info(g_display, xid);
        if (cfg) {
            short rate = g_xrr_current_rate(cfg);
            g_xrr_free_screen_config(cfg);
            if (rate > 1 && rate <= 1000) hz = (double)rate;
        }
    }
    g_xrr_cached_hz = hz;
    g_xrr_cached_ns = now_ns;
    return hz;
}

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

EXPORT void zan_gui_set_transparent(iptr hwnd_val, i32 on) {
    (void)hwnd_val;
    (void)on;
}

EXPORT void zan_gui_set_click_through(iptr hwnd_val, i32 on) {
    (void)hwnd_val;
    (void)on;
}

EXPORT i32 zan_gui_start_window_drag(iptr hwnd_val) {
    if (!g_display) return 0;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!xid) return 0;
    Window root = DefaultRootWindow(g_display);
    Window child = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(g_display, root, &root, &child, &rx, &ry, &wx, &wy, &mask)) {
        return 0;
    }
    XUngrabPointer(g_display, CurrentTime);
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = xid;
    ev.xclient.message_type = x11_atom("_NET_WM_MOVERESIZE");
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = rx;
    ev.xclient.data.l[1] = ry;
    ev.xclient.data.l[2] = 8; /* _NET_WM_MOVERESIZE_MOVE */
    ev.xclient.data.l[3] = Button1;
    ev.xclient.data.l[4] = 1;
    XSendEvent(g_display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(g_display);
    return 1;
}

EXPORT i32 zan_gui_titlebar_height(void) { return g_titlebar_h_l; }
EXPORT i32 zan_gui_caption_button_width(void) { return g_btn_w_l; }
EXPORT i32 zan_gui_safe_area_top(void) { return 0; }
EXPORT i32 zan_gui_safe_area_bottom(void) { return 0; }
EXPORT i32 zan_gui_safe_area_left(void) { return 0; }
EXPORT i32 zan_gui_safe_area_right(void) { return 0; }
/* X11 owns no software keyboard surface; no client pixels are occluded here. */
EXPORT i32 zan_gui_keyboard_height(void) { return 0; }
EXPORT i32 zan_gui_set_caption_buttons(iptr hwnd_val, i32 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count_l = (int)count; }
    return 0;
}

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
        x11_drop_destroy(w);
        zan_gui_release_window((void *)(intptr_t)w->xid);
        if (w->backbuf) XFreePixmap(g_display, w->backbuf);
        if (w->gc) XFreeGC(g_display, w->gc);
        if (w->xic) XDestroyIC(w->xic);
        XDestroyWindow(g_display, w->xid);
        int idx = (int)(w - g_lwins);
        g_lwins[idx] = g_lwins[--g_lwin_count];
    }
    if (xid == g_primary_win) {
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
        for (int i = 0; i < 8; i++) {
            if (g_cursors_linux[i]) {
                XFreeCursor(g_display, g_cursors_linux[i]);
                g_cursors_linux[i] = 0;
            }
        }
        if (g_xim) { XCloseIM(g_xim); g_xim = NULL; }
        if (g_ime_fontset_linux) {
            XFreeFontSet(g_display, g_ime_fontset_linux);
            g_ime_fontset_linux = NULL;
        }
        free(g_clip_text_linux);
        g_clip_text_linux = NULL;
        free(g_clip_read_linux);
        g_clip_read_linux = NULL;
    }
    XFlush(g_display);
    return 0;
}

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
    if (scale < 100) scale = 100;
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
    if (!g_display) return;
    Window focus = None;
    int revert = 0;
    XGetInputFocus(g_display, &focus, &revert);
    zan_lwin_t *w = lwin_find(focus);
    if (!w || !w->xic || !w->ime_position) return;
    XPoint spot;
    spot.x = (short)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
    spot.y = (short)(y < -32768 ? -32768 : y > 32767 ? 32767 : y);
    XVaNestedList attrs = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
    if (!attrs) return;
    XSetICValues(w->xic, XNPreeditAttributes, attrs, NULL);
    XFree(attrs);
    XFlush(g_display);
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
#endif
