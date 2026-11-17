/* Xdnd file transfers; included by gui_runtime_x11.c. */
#include <strings.h>

#define ZAN_X11_DROP_MAX_BYTES (4 * 1024 * 1024)
#define ZAN_X11_DROP_MAX_FILES 4096
#define ZAN_X11_DROP_TIMEOUT_MS 1000

typedef struct {
    Window hwnd;
    char **paths;
    size_t count, next;
} zan_x11_drop_batch_t;

static zan_x11_drop_batch_t *g_x11_drop_events[ZAN_EVQ_CAP];
static zan_x11_drop_batch_t *g_x11_drop_active;
static char *g_x11_drop_returned;
static Window g_x11_drop_returned_hwnd;

static void x11_drop_free(zan_x11_drop_batch_t *batch) {
    if (!batch) return;
    for (size_t i = 0; i < batch->count; i++) free(batch->paths[i]);
    free(batch->paths);
    free(batch);
}

static void x11_drop_activate(int slot) {
    /* Paths belong only to the event being dispatched, including for kind-9 blur. */
    x11_drop_free(g_x11_drop_active);
    g_x11_drop_active = g_x11_drop_events[slot];
    g_x11_drop_events[slot] = NULL;
}

EXPORT int zan_gui_drop_pending(void) {
    return g_pending_event_linux[0] == 9 && g_x11_drop_active &&
        g_x11_drop_active->hwnd == (Window)(unsigned)g_pending_event_linux[6] &&
        g_x11_drop_active->next < g_x11_drop_active->count;
}

EXPORT const char *zan_gui_drop_take(void) {
    free(g_x11_drop_returned);
    g_x11_drop_returned = NULL;
    g_x11_drop_returned_hwnd = None;
    if (!zan_gui_drop_pending()) return "";
    size_t i = g_x11_drop_active->next++;
    g_x11_drop_returned = g_x11_drop_active->paths[i];
    g_x11_drop_returned_hwnd = g_x11_drop_active->hwnd;
    g_x11_drop_active->paths[i] = NULL;
    return g_x11_drop_returned;
}

static int x11_uri_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int x11_utf8_valid(const unsigned char *s) {
    while (*s) {
        unsigned cp = *s++;
        int extra;
        unsigned min;
        if (cp < 0x80) continue;
        if (cp >= 0xc2 && cp <= 0xdf) { extra = 1; min = 0x80; cp &= 31; }
        else if (cp >= 0xe0 && cp <= 0xef) { extra = 2; min = 0x800; cp &= 15; }
        else if (cp >= 0xf0 && cp <= 0xf4) { extra = 3; min = 0x10000; cp &= 7; }
        else return 0;
        for (int i = 0; i < extra; i++) {
            if ((*s & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (*s++ & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

static char *x11_uri_path(const char *line, size_t n) {
    if (n < 6 || strncasecmp(line, "file:", 5) != 0) return NULL;
    size_t start = 5;
    if (n >= 7 && line[5] == '/' && line[6] == '/') {
        start = 7;
        size_t host = start;
        while (start < n && line[start] != '/') start++;
        if (start != host && (start - host != 9 ||
            strncasecmp(line + host, "localhost", 9) != 0)) return NULL;
    }
    if (start >= n || line[start] != '/') return NULL;
    char *path = (char *)malloc(n - start + 1);
    if (!path) return NULL;
    size_t out = 0;
    for (size_t i = start; i < n; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '?' || c == '#' || c == 0) { free(path); return NULL; }
        if (c == '%') {
            if (i + 2 >= n) { free(path); return NULL; }
            int hi = x11_uri_hex((unsigned char)line[i + 1]);
            int lo = x11_uri_hex((unsigned char)line[i + 2]);
            if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) { free(path); return NULL; }
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        }
        path[out++] = (char)c;
    }
    path[out] = 0;
    if (!x11_utf8_valid((const unsigned char *)path)) { free(path); return NULL; }
    return path;
}

static zan_x11_drop_batch_t *x11_uri_list(const char *data, size_t length) {
    zan_x11_drop_batch_t *batch = (zan_x11_drop_batch_t *)calloc(1, sizeof(*batch));
    if (!batch) return NULL;
    for (size_t start = 0; start < length;) {
        size_t end = start;
        while (end < length && data[end] != '\r' && data[end] != '\n') end++;
        if (end > start && data[start] != '#') {
            char *path = x11_uri_path(data + start, end - start);
            if (!path || batch->count >= ZAN_X11_DROP_MAX_FILES) {
                free(path); x11_drop_free(batch); return NULL;
            }
            char **paths = (char **)realloc(batch->paths, (batch->count + 1) * sizeof(*paths));
            if (!paths) { free(path); x11_drop_free(batch); return NULL; }
            batch->paths = paths;
            batch->paths[batch->count++] = path;
        }
        start = end;
        while (start < length && (data[start] == '\r' || data[start] == '\n')) start++;
    }
    if (!batch->count) { x11_drop_free(batch); return NULL; }
    return batch;
}

/* Xdnd sources may disappear between messages. Trap only this UI-thread request. */
static unsigned long g_x11_dnd_serial;
static XErrorHandler g_x11_dnd_previous_error;
static int x11_dnd_error(Display *display, XErrorEvent *error) {
    if (display == g_display && error->serial == g_x11_dnd_serial &&
        error->error_code == BadWindow) return 0;
    return g_x11_dnd_previous_error ? g_x11_dnd_previous_error(display, error) : 0;
}

static void x11_dnd_reply(Window target, Window source, const char *type,
                          long accepted, long action) {
    if (!source) return;
    XEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.xclient.type = ClientMessage;
    reply.xclient.window = source;
    reply.xclient.message_type = x11_atom(type);
    reply.xclient.format = 32;
    reply.xclient.data.l[0] = (long)target;
    reply.xclient.data.l[1] = accepted;
    if (strcmp(type, "XdndStatus") == 0) reply.xclient.data.l[4] = action;
    else reply.xclient.data.l[2] = action;
    g_x11_dnd_previous_error = XSetErrorHandler(x11_dnd_error);
    g_x11_dnd_serial = NextRequest(g_display);
    XSendEvent(g_display, source, False, NoEventMask, &reply);
    /* This delivers X errors; the event pump still owns protocol dispatch. */
    XSync(g_display, False);
    XSetErrorHandler(g_x11_dnd_previous_error);
}

static void x11_dnd_reset(zan_lwin_t *w) {
    if (w->dnd.requestor) XDestroyWindow(g_display, w->dnd.requestor);
    memset(&w->dnd, 0, sizeof(w->dnd));
}

/* Cleanup is driven by the GUI pump, even when no X event arrives. */
static int x11_dnd_poll_timeout(int timeout) {
    i64 now = zan_gui_get_tick_ms();
    for (int i = 0; i < g_lwin_count; i++) {
        zan_lwin_t *w = &g_lwins[i];
        if (!w->dnd.pending) continue;
        i64 remaining = w->dnd.deadline - now;
        if (remaining <= 0) {
            x11_dnd_reply(w->xid, w->dnd.source, "XdndFinished", 0, None);
            x11_dnd_reset(w);
            XFlush(g_display);
        } else if (timeout < 0 || remaining < timeout) {
            timeout = (int)remaining;
        }
    }
    return timeout;
}

static void x11_drop_destroy(zan_lwin_t *w) {
    if (w->dnd.pending)
        x11_dnd_reply(w->xid, w->dnd.source, "XdndFinished", 0, None);
    x11_dnd_reset(w);
    if (g_x11_drop_active && g_x11_drop_active->hwnd == w->xid) {
        x11_drop_free(g_x11_drop_active);
        g_x11_drop_active = NULL;
    }
    for (int i = 0; i < ZAN_EVQ_CAP; i++) {
        if (g_x11_drop_events[i] && g_x11_drop_events[i]->hwnd == w->xid) {
            x11_drop_free(g_x11_drop_events[i]);
            g_x11_drop_events[i] = NULL;
            g_evq_linux[i][0] = 0;
        }
    }
    if (g_x11_drop_returned_hwnd == w->xid) {
        free(g_x11_drop_returned);
        g_x11_drop_returned = NULL;
        g_x11_drop_returned_hwnd = None;
    }
}

static int x11_dnd_client(XClientMessageEvent *event) {
    zan_lwin_t *w = lwin_find(event->window);
    if (!w || event->format != 32) return 0;
    Window source = (Window)(uint32_t)event->data.l[0];
    if (event->message_type == x11_atom("XdndEnter")) {
        /* A second gesture must not steal the selection conversion in flight. */
        if (w->dnd.pending) return 1;
        x11_dnd_reset(w);
        w->dnd.source = source;
        unsigned version = ((unsigned long)event->data.l[1] >> 24) & 255;
        if (!source || version < 3) return 1;
        Atom uri = x11_atom("text/uri-list");
        if (event->data.l[1] & 1) {
            Atom type; int format; unsigned long count, after;
            unsigned char *data = NULL;
            Atom type_list = x11_atom("XdndTypeList");
            g_x11_dnd_previous_error = XSetErrorHandler(x11_dnd_error);
            g_x11_dnd_serial = NextRequest(g_display);
            int rc = XGetWindowProperty(g_display, source, type_list,
                0, 4096, False, XA_ATOM, &type, &format, &count, &after, &data);
            XSetErrorHandler(g_x11_dnd_previous_error);
            if (rc == Success && type == XA_ATOM && format == 32 && after == 0 && data) {
                for (unsigned long i = 0; i < count; i++)
                    if (((Atom *)data)[i] == uri) w->dnd.offered = 1;
            }
            if (data) XFree(data);
        } else {
            for (int i = 2; i <= 4; i++)
                if ((Atom)event->data.l[i] == uri) w->dnd.offered = 1;
        }
        return 1;
    }
    if (!source || source != w->dnd.source) return 0;
    if (event->message_type == x11_atom("XdndPosition")) {
        if (w->dnd.pending) return 1;
        int root_x = (short)((unsigned long)event->data.l[2] >> 16);
        int root_y = (short)(event->data.l[2] & 65535);
        Window child;
        w->dnd.accepted = w->dnd.offered &&
            ((Atom)event->data.l[4] == x11_atom("XdndActionCopy")) &&
            XTranslateCoordinates(g_display, DefaultRootWindow(g_display), w->xid,
                                  root_x, root_y, &w->dnd.x, &w->dnd.y, &child) &&
            w->dnd.x >= 0 && w->dnd.y >= 0 && w->dnd.x < w->w && w->dnd.y < w->h;
        x11_dnd_reply(w->xid, source, "XdndStatus", w->dnd.accepted ? 3 : 2,
                      w->dnd.accepted ? (long)x11_atom("XdndActionCopy") : None);
        return 1;
    }
    if (event->message_type == x11_atom("XdndLeave")) {
        if (!w->dnd.pending) x11_dnd_reset(w);
        return 1;
    }
    if (event->message_type == x11_atom("XdndDrop")) {
        if (w->dnd.pending) return 1;
        if (!w->dnd.accepted ||
            XGetSelectionOwner(g_display, x11_atom("XdndSelection")) != source) {
            x11_dnd_reply(w->xid, source, "XdndFinished", 0, None);
            x11_dnd_reset(w);
            return 1;
        }
        /* A private requestor correlates late replies without leaking per-drop atoms. */
        w->dnd.requestor = XCreateSimpleWindow(g_display, DefaultRootWindow(g_display),
                                              0, 0, 1, 1, 0, 0, 0);
        w->dnd.pending = 1;
        w->dnd.deadline = zan_gui_get_tick_ms() + ZAN_X11_DROP_TIMEOUT_MS;
        XConvertSelection(g_display, x11_atom("XdndSelection"), x11_atom("text/uri-list"),
                          x11_atom("ZAN_GUI_XDND"), w->dnd.requestor, (Time)event->data.l[2]);
        XFlush(g_display);
        return 1;
    }
    return 0;
}

static void x11_dnd_selection(XSelectionEvent *event) {
    x11_dnd_poll_timeout(0);
    if (event->selection != x11_atom("XdndSelection")) return;
    zan_lwin_t *w = NULL;
    for (int i = 0; i < g_lwin_count; i++) {
        if (g_lwins[i].dnd.pending && g_lwins[i].dnd.requestor == event->requestor) {
            w = &g_lwins[i];
            break;
        }
    }
    if (!w) return;
    zan_x11_drop_batch_t *batch = NULL;
    Atom property = x11_atom("ZAN_GUI_XDND");
    if (event->property == property && event->target == x11_atom("text/uri-list") &&
        XGetSelectionOwner(g_display, event->selection) == w->dnd.source) {
        Atom type; int format; unsigned long count, after;
        unsigned char *data = NULL;
        /* Do not delete/acknowledge an INCR header: chunked transfers are unsupported. */
        if (XGetWindowProperty(g_display, event->requestor, property,
            0, ZAN_X11_DROP_MAX_BYTES / 4, False, AnyPropertyType,
            &type, &format, &count, &after, &data) == Success &&
            type == x11_atom("text/uri-list") && format == 8 && after == 0 && data)
            batch = x11_uri_list((const char *)data, (size_t)count);
        if (data) XFree(data);
    }
    int slot = g_evq_tail_linux;
    if (batch && (slot + 1) % ZAN_EVQ_CAP != g_evq_head_linux) {
        batch->hwnd = w->xid;
        g_evwin_linux = w->xid;
        g_x11_drop_events[slot] = batch;
        evq_push_linux(9, w->dnd.x, w->dnd.y, 0, 0, 0);
    } else {
        x11_drop_free(batch);
        batch = NULL;
    }
    x11_dnd_reply(w->xid, w->dnd.source, "XdndFinished", batch ? 1 : 0,
                  batch ? (long)x11_atom("XdndActionCopy") : None);
    x11_dnd_reset(w);
}
