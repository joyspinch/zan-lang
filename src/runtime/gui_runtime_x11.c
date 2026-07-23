/* gui_runtime_x11.c -- the Linux X11 window shell.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * Linux X11 Window Shell
 * ======================================================================== */

/* Compiled out when the unified SDL backend owns windowing (ZAN_GUI_SDL); the
 * shared software rasterizer and FreeType/software text above still build. */
#if defined(__linux__) && !defined(ZAN_GUI_SDL)

static Display *g_display = NULL;
static Window g_x11_window = 0;
/* Self-pipe used to wake a UI thread blocked in wait_event from another
 * thread (Xlib is not thread-safe, so we can't post an X event off-thread;
 * writing a byte to a pipe that wait_event also polls is safe). */
static int g_wake_pipe[2] = { -1, -1 };
static int g_pending_event_linux[8];
static int g_win_w = 0, g_win_h = 0;
static char *g_clip_text_linux = NULL;
static char *g_clip_read_linux = NULL;
static Cursor g_cursors_linux[8];
static XIM g_xim = NULL;
static XIC g_xic = NULL;

/* Client-side decoration metrics for the borderless window with an app-drawn
 * title bar, mirroring the Win32 backend. */
static int g_scale_linux = 100;
static int g_titlebar_h_l = 32;
static int g_btn_w_l = 46;
static int g_caption_btn_count_l = 5;

/* Per-window state so one process can drive several top-level windows. The
 * globals above still track the primary window for process-wide operations
 * (clipboard, cursor, input method) and single-window size queries. */
#define ZAN_MAX_WINDOWS 16
typedef struct {
    Window xid;
    GC gc;
    XIC xic;
    Pixmap backbuf;
    int backbuf_w, backbuf_h;
    int w, h;
} zan_lwin_t;
static zan_lwin_t g_lwins[ZAN_MAX_WINDOWS];
static int g_lwin_count = 0;
static Window g_primary_win = 0;
/* xid of the window the event currently being decoded originated from. */
static Window g_evwin_linux = 0;

static zan_lwin_t *lwin_find(Window xid) {
    for (int i = 0; i < g_lwin_count; i++)
        if (g_lwins[i].xid == xid) return &g_lwins[i];
    return NULL;
}

i64 zan_gui_get_dpi_scale(void);
i64 zan_gui_is_maximized(i64 hwnd_val);

/* Decoded-event queue: a single XEvent can yield several ABI events (a key
 * press -> keyDown + textInput; a wheel button -> scroll), so decoded events
 * are buffered and drained one per poll/wait call, mirroring the Win32 message
 * queue. Slots: [kind, x, y, button, keycode, mods, 0, 0]. */
#define ZAN_EVQ_CAP 64
static int g_evq_linux[ZAN_EVQ_CAP][8];
static int g_evq_head_linux = 0, g_evq_tail_linux = 0;

static void evq_push_linux(int kind, int x, int y, int button, int keycode, int mods) {
    int next = (g_evq_tail_linux + 1) % ZAN_EVQ_CAP;
    if (next == g_evq_head_linux) return; /* queue full: drop */
    int *e = g_evq_linux[g_evq_tail_linux];
    e[0] = kind; e[1] = x; e[2] = y; e[3] = button;
    e[4] = keycode; e[5] = mods; e[6] = (int)g_evwin_linux; e[7] = 0;
    g_evq_tail_linux = next;
}

static int evq_pop_linux(void) {
    if (g_evq_head_linux == g_evq_tail_linux) return 0;
    memcpy(g_pending_event_linux, g_evq_linux[g_evq_head_linux],
           sizeof(g_pending_event_linux));
    g_evq_head_linux = (g_evq_head_linux + 1) % ZAN_EVQ_CAP;
    return 1;
}

/* Decode one UTF-8 scalar, advancing *p past it. */
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

/* X11 modifier state -> Win32 encoding (bit0=Ctrl, bit1=Shift, bit2=Alt). */
static int x11_mods(unsigned int state) {
    int m = 0;
    if (state & ControlMask) m |= 1;
    if (state & ShiftMask)   m |= 2;
    if (state & Mod1Mask)    m |= 4;
    return m;
}

/* X keysym -> Windows VK code the Zan `Keys` constants use. */
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

/* Toggle/set an EWMH _NET_WM_STATE property via the window manager.
 * action: 0 = remove, 1 = add, 2 = toggle. state2 may be 0. */
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

/* _NET_WM_MOVERESIZE directions. */
#define ZAN_NWMR_TOPLEFT     0
#define ZAN_NWMR_TOP         1
#define ZAN_NWMR_TOPRIGHT    2
#define ZAN_NWMR_RIGHT       3
#define ZAN_NWMR_BOTTOMRIGHT 4
#define ZAN_NWMR_BOTTOM      5
#define ZAN_NWMR_BOTTOMLEFT  6
#define ZAN_NWMR_LEFT        7
#define ZAN_NWMR_MOVE        8

/* Remove window-manager decorations via the Motif hint so the app can draw its
 * own title bar (matching the Win32 borderless window). */
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

/* Ask the window manager to start an interactive move/resize, so the app-drawn
 * caption and resize borders behave like real ones. */
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
    xev.xclient.data.l[4] = 1; /* source indication: application */
    XSendEvent(g_display, DefaultRootWindow(g_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(g_display);
}

/* Map a left press to a move/resize direction, mirroring the Win32
 * WM_NCHITTEST logic: 8px resize borders plus a draggable caption that excludes
 * the caption-button cluster. Returns -1 for an ordinary client click. */
static int x11_caption_hit(zan_lwin_t *lw, int x, int y) {
    int w = lw->w, h = lw->h;
    int capW = g_caption_btn_count_l * g_btn_w_l;
    int inCaptionDrag = (y < g_titlebar_h_l && x < w - capW);
    if (zan_gui_is_maximized((i64)lw->xid)) {
        return inCaptionDrag ? ZAN_NWMR_MOVE : -1;
    }
    int b = 8 * g_scale_linux / 100;
    if (b < 4) b = 4;
    int left = x < b, right = x >= w - b, top = y < b, bottom = y >= h - b;
    if (top && left) return ZAN_NWMR_TOPLEFT;
    if (top && right) return ZAN_NWMR_TOPRIGHT;
    if (bottom && left) return ZAN_NWMR_BOTTOMLEFT;
    if (bottom && right) return ZAN_NWMR_BOTTOMRIGHT;
    if (left) return ZAN_NWMR_LEFT;
    if (right) return ZAN_NWMR_RIGHT;
    if (top) return ZAN_NWMR_TOP;
    if (bottom) return ZAN_NWMR_BOTTOM;
    if (inCaptionDrag) return ZAN_NWMR_MOVE;
    return -1;
}

/* Serve a clipboard paste request from another client (we own CLIPBOARD). */
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

/* Decode a raw XEvent into zero or more queued ABI events. */
static void x11_translate_event(XEvent *ev) {
    g_evwin_linux = ev->xany.window;
    switch (ev->type) {
    case MotionNotify:
        evq_push_linux(1, ev->xmotion.x, ev->xmotion.y, 0, 0,
                       x11_mods(ev->xmotion.state));
        break;
    case ButtonPress:
        /* Buttons 4/5 are the vertical wheel; report them as scroll (kind 13)
         * with a Win32-style +/-120 delta. Buttons 6/7 (horizontal) have no
         * ABI and are ignored. */
        if (ev->xbutton.button == 4 || ev->xbutton.button == 5) {
            int delta = ev->xbutton.button == 4 ? 120 : -120;
            evq_push_linux(13, ev->xbutton.x, ev->xbutton.y, 0, delta,
                           x11_mods(ev->xbutton.state));
        } else if (ev->xbutton.button == 6 || ev->xbutton.button == 7) {
            /* horizontal wheel: ignored */
        } else if (ev->xbutton.button == 1) {
            /* Honor the app-drawn caption and resize borders by delegating to
             * the WM, mirroring Win32 WM_NCHITTEST. Presses over content or the
             * caption buttons fall through as ordinary clicks. */
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
                           (int)ev->xbutton.button - 1, 0,
                           x11_mods(ev->xbutton.state));
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button >= 4 && ev->xbutton.button <= 7) break;
        evq_push_linux(3, ev->xbutton.x, ev->xbutton.y,
                       (int)ev->xbutton.button - 1, 0,
                       x11_mods(ev->xbutton.state));
        break;
    case KeyPress: {
        int mods = x11_mods(ev->xkey.state);
        KeySym ks = 0;
        char buf[32];
        int n;
        zan_lwin_t *kw = lwin_find(ev->xkey.window);
        XIC xic = (kw && kw->xic) ? kw->xic : g_xic;
        if (xic) {
            Status st = 0;
            n = Xutf8LookupString(xic, &ev->xkey, buf, sizeof(buf) - 1, &ks, &st);
        } else {
            n = XLookupString(&ev->xkey, buf, sizeof(buf) - 1, &ks, NULL);
        }
        evq_push_linux(4, 0, 0, 0, x11_vk_from_keysym(ks), mods);
        /* Emit a WM_CHAR-style text event for each decoded character. X already
         * folds Ctrl combos to control codes (e.g. Ctrl+C -> 0x03) and delivers
         * Backspace/Tab/Enter as 8/9/13, exactly like Win32 WM_CHAR; the widgets
         * rely on those integer codes for both typing and shortcuts. */
        if (n > 0) {
            buf[n] = '\0';
            const char *p = buf;
            while (*p) {
                unsigned cp = x11_utf8_next(&p);
                if (cp != 0 && cp != 127)
                    evq_push_linux(6, 0, 0, 0, (int)cp, mods);
            }
        }
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
        /* Re-blit the last frame from the window's back buffer so uncover/move
         * never leaves stale or blank content. */
        zan_lwin_t *ew = lwin_find(ev->xexpose.window);
        if (ew && ew->backbuf)
            XCopyArea(g_display, ew->backbuf, ew->xid, ew->gc, 0, 0,
                      (unsigned)ew->backbuf_w, (unsigned)ew->backbuf_h, 0, 0);
        break;
    }
    case ClientMessage:
        evq_push_linux(8, 0, 0, 0, 0, 0);
        break;
    }
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    if (!g_display) {
        g_display = XOpenDisplay(NULL);
        if (!g_display) return 0;
        /* Input method for UTF-8 text input (also enables IME preedit); opened
         * once per display and shared by every window's input context. */
        setlocale(LC_ALL, "");
        XSetLocaleModifiers("");
        g_xim = XOpenIM(g_display, NULL, NULL, NULL);
        if (g_wake_pipe[0] < 0 && pipe(g_wake_pipe) == 0) {
            fcntl(g_wake_pipe[0], F_SETFL, O_NONBLOCK);
            fcntl(g_wake_pipe[1], F_SETFL, O_NONBLOCK);
            fcntl(g_wake_pipe[0], F_SETFD, FD_CLOEXEC);
            fcntl(g_wake_pipe[1], F_SETFD, FD_CLOEXEC);
        }
    }
    if (g_lwin_count >= ZAN_MAX_WINDOWS) return 0;

    int screen = DefaultScreen(g_display);
    Window xid = XCreateSimpleWindow(g_display, RootWindow(g_display, screen),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(g_display, screen), WhitePixel(g_display, screen));

    XStoreName(g_display, xid, title);
    XSelectInput(g_display, xid,
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        StructureNotifyMask);

    Atom wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, xid, &wm_delete, 1);

    XIC xic = NULL;
    if (g_xim) {
        xic = XCreateIC(g_xim,
            XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
            XNClientWindow, xid,
            XNFocusWindow, xid,
            NULL);
        if (xic) XSetICFocus(xic);
    }
    GC gc = XCreateGC(g_display, xid, 0, NULL);

    zan_lwin_t *w = &g_lwins[g_lwin_count++];
    w->xid = xid;
    w->gc = gc;
    w->xic = xic;
    w->backbuf = 0;
    w->backbuf_w = 0;
    w->backbuf_h = 0;
    w->w = (int)width;
    w->h = (int)height;

    if (!g_primary_win) {
        /* First window drives process-wide operations (clipboard, cursor) and
         * the client-side title-bar metrics computed once from its DPI. */
        g_primary_win = xid;
        g_x11_window = xid;
        g_xic = xic;
        g_win_w = (int)width;
        g_win_h = (int)height;
        g_scale_linux = (int)zan_gui_get_dpi_scale();
        g_titlebar_h_l = 32 * g_scale_linux / 100;
        g_btn_w_l = 46 * g_scale_linux / 100;
    } else {
        XWindowAttributes parent_attr;
        Window child = 0;
        int parent_x = 0;
        int parent_y = 0;
        Window root = RootWindow(g_display, screen);
        if (XGetWindowAttributes(g_display, g_primary_win, &parent_attr) &&
            XTranslateCoordinates(g_display, g_primary_win, root, 0, 0,
                                  &parent_x, &parent_y, &child)) {
            int x = parent_x + (parent_attr.width - (int)width) / 2;
            int y = parent_y + (parent_attr.height - (int)height) / 2;
            int screen_w = DisplayWidth(g_display, screen);
            int screen_h = DisplayHeight(g_display, screen);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + (int)width > screen_w) x = screen_w - (int)width;
            if (y + (int)height > screen_h) y = screen_h - (int)height;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            XMoveWindow(g_display, xid, x, y);
        }
    }
    /* Borderless window with app-drawn title bar (matches the Win32 backend). */
    x11_set_borderless(xid);

    return (i64)xid;
}

EXPORT i64 zan_gui_show_window(i64 win) {
    Window xid = win ? (Window)(intptr_t)win : g_x11_window;
    if (g_display && xid) {
        XMapWindow(g_display, xid);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (evq_pop_linux()) return 0;

    int xfd = ConnectionNumber(g_display);
    XEvent ev;
    for (;;) {
        /* Drain any X events already buffered in the client before blocking. */
        while (XPending(g_display) > 0) {
            XNextEvent(g_display, &ev);
            if (ev.type == SelectionRequest) {
                x11_serve_selection(&ev.xselectionrequest);
                continue;
            }
            if (XFilterEvent(&ev, None)) continue;
            x11_translate_event(&ev);
            if (evq_pop_linux()) return 0;
        }
        /* Block until the X connection or the wake pipe becomes readable. */
        struct pollfd fds[2];
        fds[0].fd = xfd; fds[0].events = POLLIN; fds[0].revents = 0;
        int nfds = 1;
        if (g_wake_pipe[0] >= 0) {
            fds[1].fd = g_wake_pipe[0]; fds[1].events = POLLIN; fds[1].revents = 0;
            nfds = 2;
        }
        int pr = poll(fds, (nfds_t)nfds, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            char buf[64];
            while (read(g_wake_pipe[0], buf, sizeof(buf)) > 0) { }
            /* Return a benign empty frame (kind 0) so the caller drains its
             * dispatch queue. Any pending X events are handled next loop. */
            return 0;
        }
        /* X connection readable: loop back to XPending/XNextEvent. */
    }
}

/* Like wait_event but gives up after `ms` milliseconds. Returns 0 when an
 * event was delivered, 1 on timeout, -1 on error. Lets an animation loop idle
 * in the kernel until either input arrives or its next frame deadline. */
EXPORT i64 zan_gui_wait_event_timeout(i64 ms) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (evq_pop_linux()) return 0;

    int xfd = ConnectionNumber(g_display);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    XEvent ev;
    for (;;) {
        while (XPending(g_display) > 0) {
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
        int pr = poll(fds, (nfds_t)nfds, (int)remain);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) return 1;
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            char buf[64];
            while (read(g_wake_pipe[0], buf, sizeof(buf)) > 0) { }
            return 0;
        }
    }
}

/* Wake a UI thread blocked in wait_event so it can drain the dispatch queue.
 * write() is async-signal-safe and thread-safe, so this is callable from any
 * thread even though Xlib is not. */
EXPORT i64 zan_gui_wake(void) {
    if (g_wake_pipe[1] >= 0) {
        char b = 1;
        ssize_t n = write(g_wake_pipe[1], &b, 1);
        (void)n;
    }
    return 0;
}

EXPORT i64 zan_gui_poll_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (evq_pop_linux()) return 0;
    for (;;) {
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

/* Queue a synthetic input event for the automation driver (see Gui.UiDriver);
 * drained by poll/wait_event like a real X11 event so App dispatch is
 * exercised unchanged. Called on the UI thread. hwnd_val is unused here (the
 * X11 backend tracks the source window internally). */
EXPORT i64 zan_gui_inject_event(i64 hwnd_val, i64 kind, i64 x, i64 y,
                                i64 button, i64 keycode, i64 mods) {
    (void)hwnd_val;
    evq_push_linux((int)kind, (int)x, (int)y, (int)button, (int)keycode, (int)mods);
    zan_gui_wake();
    return 0;
}

EXPORT i64 zan_gui_inject_pending(void) {
    return (g_evq_tail_linux - g_evq_head_linux + ZAN_EVQ_CAP) % ZAN_EVQ_CAP;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event_linux[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event_linux[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event_linux[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event_linux[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event_linux[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event_linux[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_win_w; }
EXPORT i64 zan_gui_window_height(void) { return g_win_h; }

EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)(unsigned)g_pending_event_linux[6]; }
EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->w : g_win_w;
}
EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->h : g_win_h;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    if (!g_display) return 1;
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    if (!w && g_lwin_count > 0) w = &g_lwins[0];
    if (!w) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    int screen = DefaultScreen(g_display);
    unsigned depth = (unsigned)DefaultDepth(g_display, screen);

    /* Blit through the window's own off-screen Pixmap (double buffering) so
     * resizes and expose events never show a half-drawn or torn frame. */
    if (!w->backbuf || w->backbuf_w != s->width || w->backbuf_h != s->height) {
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
        XPutImage(g_display, w->backbuf, w->gc, img, 0, 0, 0, 0,
                  (unsigned)s->width, (unsigned)s->height);
        img->data = NULL;
        XDestroyImage(img);
        XCopyArea(g_display, w->backbuf, w->xid, w->gc, 0, 0,
                  (unsigned)s->width, (unsigned)s->height, 0, 0);
    }
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XStoreName(g_display, xid, title);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
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

EXPORT void zan_gui_sleep_ms(i64 ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&req, NULL);
}
#endif /* __linux__ && !ZAN_GUI_SDL (X11 window shell) */
