#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <dlfcn.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern intptr_t zan_gui_create_window(const char *, int32_t, int32_t);
extern int32_t zan_gui_show_window(intptr_t);
extern int32_t zan_gui_destroy_window(intptr_t);
extern int zan_gui_resize_client(intptr_t, int32_t, int32_t);
extern int32_t zan_gui_client_width(intptr_t);
extern int32_t zan_gui_client_height(intptr_t);
extern int32_t zan_gui_poll_event(void);
extern int32_t zan_gui_wait_event(void);
extern int32_t zan_gui_wait_event_timeout(int32_t);
extern int32_t zan_gui_event_kind(void);
extern intptr_t zan_gui_event_hwnd(void);
extern int32_t zan_gui_event_x(void);
extern int32_t zan_gui_event_y(void);
extern int32_t zan_gui_event_keycode(void);
extern int32_t zan_gui_inject_event(intptr_t, int32_t, int32_t, int32_t,
                                   int32_t, int32_t, int32_t);
extern int32_t zan_gui_inject_pending(void);
extern int zan_gui_drop_pending(void);
extern const char *zan_gui_drop_take(void);
extern void zan_gui_set_ime_pos(int32_t, int32_t);

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

#define LOAD_NEXT(name) do { \
    if (!next) *(void **)(&next) = dlsym(RTLD_NEXT, #name); \
    CHECK(next); \
} while (0)

static Display *display;
static const char *test_stage = "initialization";
static unsigned drop_sequence;
static Window queue_window;
static int queue_fill, queue_seen;
static const int queue_marker = 27001;

/* Interpose only the selected boundary; protocol requests still use the X server. */
int XGetWindowProperty(Display *d, Window w, Atom property, long offset, long length,
                       Bool remove, Atom requested, Atom *type, int *format,
                       unsigned long *count, unsigned long *after, unsigned char **data) {
    static int (*next)(Display *, Window, Atom, long, long, Bool, Atom,
                       Atom *, int *, unsigned long *, unsigned long *, unsigned char **);
    LOAD_NEXT(XGetWindowProperty);
    int rc = next(d, w, property, offset, length, remove, requested,
                  type, format, count, after, data);
    if (queue_fill && property == XInternAtom(d, "ZAN_GUI_XDND", False)) {
        int fill = queue_fill;
        queue_fill = 0;
        CHECK(zan_gui_inject_pending() == 0);
        for (int i = 0; i < fill; i++)
            CHECK(zan_gui_inject_event((intptr_t)queue_window, 1, queue_marker,
                                       i, 0, 0, 0) == 0);
        CHECK(zan_gui_inject_pending() == fill);
    }
    return rc;
}

typedef struct {
    Window window;
    int focused, set_count, unset_count, spot_count, destroyed;
    XPoint spot;
} test_ic_t;

static test_ic_t fake_ics[4];
static int fake_ic_count, fake_im_token;
static int inject_im, inject_position = 1, fail_create_ic;
static int lookup_mode, lookup_calls;
static XIC lookup_ic;
static XVaNestedList last_nested;
static XPoint last_spot;
static int nested_has_spot, nested_has_font;
static int system_style_queries, system_position_style, system_position_created;

static test_ic_t *fake_ic(XIC ic) {
    for (int i = 0; i < fake_ic_count; i++)
        if (ic == (XIC)&fake_ics[i]) return &fake_ics[i];
    return NULL;
}

XIM XOpenIM(Display *d, XrmDatabase db, char *resource, char *class_name) {
    static XIM (*next)(Display *, XrmDatabase, char *, char *);
    if (inject_im) return (XIM)&fake_im_token;
    LOAD_NEXT(XOpenIM);
    return next(d, db, resource, class_name);
}

Status XCloseIM(XIM im) {
    static Status (*next)(XIM);
    if (im == (XIM)&fake_im_token) return 1;
    LOAD_NEXT(XCloseIM);
    return next(im);
}

char *XGetIMValues(XIM im, ...) {
    static char *(*next)(XIM, ...);
    va_list args;
    va_start(args, im);
    char *name = va_arg(args, char *);
    XIMStyles **out = va_arg(args, XIMStyles **);
    CHECK(strcmp(name, XNQueryInputStyle) == 0);
    CHECK(va_arg(args, char *) == NULL);
    va_end(args);
    if (im == (XIM)&fake_im_token) {
        XIMStyles *styles = calloc(1, sizeof(*styles) + sizeof(XIMStyle));
        CHECK(styles);
        styles->count_styles = 1;
        styles->supported_styles = (XIMStyle *)(styles + 1);
        styles->supported_styles[0] = inject_position ?
            XIMPreeditPosition | XIMStatusNothing : XIMPreeditNothing | XIMStatusNothing;
        *out = styles;
        return NULL;
    }
    LOAD_NEXT(XGetIMValues);
    char *error = next(im, name, out, NULL);
    system_style_queries++;
    if (!error && *out) {
        for (unsigned short i = 0; i < (*out)->count_styles; i++)
            if ((*out)->supported_styles[i] == (XIMPreeditPosition | XIMStatusNothing))
                system_position_style = 1;
    }
    return error;
}

XVaNestedList XVaCreateNestedList(int unused, ...) {
    static XVaNestedList (*next)(int, ...);
    va_list args;
    va_start(args, unused);
    char *name;
    XFontSet font = NULL;
    nested_has_spot = nested_has_font = 0;
    while ((name = va_arg(args, char *)) != NULL) {
        if (strcmp(name, XNSpotLocation) == 0) {
            last_spot = *va_arg(args, XPoint *);
            nested_has_spot = 1;
        } else if (strcmp(name, XNFontSet) == 0) {
            font = va_arg(args, XFontSet);
            nested_has_font = font != NULL;
        } else {
            CHECK(0);
        }
    }
    va_end(args);
    CHECK(nested_has_spot);
    LOAD_NEXT(XVaCreateNestedList);
    last_nested = font ? next(unused, XNSpotLocation, &last_spot, XNFontSet, font, NULL) :
                         next(unused, XNSpotLocation, &last_spot, NULL);
    return last_nested;
}

XIC XCreateIC(XIM im, ...) {
    static XIC (*next)(XIM, ...);
    va_list args;
    va_start(args, im);
    char *name;
    XIMStyle style = 0;
    Window client = None, focus = None;
    XVaNestedList preedit = NULL;
    while ((name = va_arg(args, char *)) != NULL) {
        if (strcmp(name, XNInputStyle) == 0) style = va_arg(args, XIMStyle);
        else if (strcmp(name, XNClientWindow) == 0) client = va_arg(args, Window);
        else if (strcmp(name, XNFocusWindow) == 0) focus = va_arg(args, Window);
        else if (strcmp(name, XNPreeditAttributes) == 0) preedit = va_arg(args, XVaNestedList);
        else CHECK(0);
    }
    va_end(args);
    CHECK(client && focus == client);
    if (im == (XIM)&fake_im_token) {
        if (fail_create_ic) return NULL;
        CHECK(fake_ic_count < 4);
        if (style & XIMPreeditPosition)
            CHECK(preedit && preedit == last_nested && nested_has_font);
        test_ic_t *ic = &fake_ics[fake_ic_count++];
        memset(ic, 0, sizeof(*ic));
        ic->window = client;
        return (XIC)ic;
    }
    LOAD_NEXT(XCreateIC);
    XIC ic = preedit ? next(im, XNInputStyle, style, XNClientWindow, client,
                           XNFocusWindow, focus, XNPreeditAttributes, preedit, NULL) :
                       next(im, XNInputStyle, style, XNClientWindow, client,
                           XNFocusWindow, focus, NULL);
    if (ic && (style & XIMPreeditPosition)) system_position_created = 1;
    return ic;
}

char *XGetICValues(XIC ic, ...) {
    static char *(*next)(XIC, ...);
    va_list args;
    va_start(args, ic);
    char *name = va_arg(args, char *);
    long *mask = va_arg(args, long *);
    CHECK(strcmp(name, XNFilterEvents) == 0);
    CHECK(va_arg(args, char *) == NULL);
    va_end(args);
    if (fake_ic(ic)) { *mask = KeyPressMask | KeyReleaseMask | PropertyChangeMask; return NULL; }
    LOAD_NEXT(XGetICValues);
    return next(ic, name, mask, NULL);
}

char *XSetICValues(XIC ic, ...) {
    static char *(*next)(XIC, ...);
    va_list args;
    va_start(args, ic);
    char *name = va_arg(args, char *);
    XVaNestedList attrs = va_arg(args, XVaNestedList);
    CHECK(strcmp(name, XNPreeditAttributes) == 0);
    CHECK(va_arg(args, char *) == NULL);
    va_end(args);
    test_ic_t *test = fake_ic(ic);
    if (test) {
        CHECK(!test->destroyed && attrs == last_nested && nested_has_spot);
        test->spot = last_spot;
        test->spot_count++;
        return NULL;
    }
    LOAD_NEXT(XSetICValues);
    return next(ic, name, attrs, NULL);
}

void XSetICFocus(XIC ic) {
    static void (*next)(XIC);
    test_ic_t *test = fake_ic(ic);
    if (test) { CHECK(!test->destroyed); test->focused = 1; test->set_count++; return; }
    LOAD_NEXT(XSetICFocus);
    next(ic);
}

void XUnsetICFocus(XIC ic) {
    static void (*next)(XIC);
    test_ic_t *test = fake_ic(ic);
    if (test) { CHECK(!test->destroyed); test->focused = 0; test->unset_count++; return; }
    LOAD_NEXT(XUnsetICFocus);
    next(ic);
}

void XDestroyIC(XIC ic) {
    static void (*next)(XIC);
    test_ic_t *test = fake_ic(ic);
    if (test) { CHECK(!test->destroyed); test->destroyed = 1; return; }
    LOAD_NEXT(XDestroyIC);
    next(ic);
}

int Xutf8LookupString(XIC ic, XKeyPressedEvent *event, char *buffer, int capacity,
                      KeySym *keysym, Status *status) {
    static int (*next)(XIC, XKeyPressedEvent *, char *, int, KeySym *, Status *);
    if (!fake_ic(ic)) {
        LOAD_NEXT(Xutf8LookupString);
        return next(ic, event, buffer, capacity, keysym, status);
    }
    lookup_ic = ic;
    lookup_calls++;
    *keysym = XK_a;
    if (lookup_mode == 1) {
        const int bytes = 74; /* 24 Chinese code points followed by space and z. */
        if (capacity < bytes) { *status = XBufferOverflow; return bytes; }
        for (int i = 0; i < 24; i++) memcpy(buffer + 3 * i, "\xe4\xb8\xad", 3);
        memcpy(buffer + 72, " z", 2);
        *status = XLookupBoth;
        return bytes;
    }
    if (lookup_mode == 2) { *status = XBufferOverflow; return lookup_calls == 1 ? 80 : 100; }
    if (lookup_mode == 3) { *status = XBufferOverflow; return 1024 * 1024 + 1; }
    if (lookup_mode == 4) { *status = XLookupChars; return capacity + 1; }
    if (lookup_mode == 5) { buffer[0] = (char)0xe4; *status = XLookupChars; return 1; }
    *status = XLookupNone;
    return 0;
}

static Atom atom(const char *name) { return XInternAtom(display, name, False); }

static int poll_no_drop(void) {
    int rc = zan_gui_poll_event();
    CHECK(rc >= 0);
    if (rc == 0) {
        CHECK(zan_gui_event_kind() != 8);
        CHECK(!zan_gui_drop_pending());
        if (zan_gui_event_kind() == 1 && zan_gui_event_x() == queue_marker) {
            CHECK(zan_gui_event_hwnd() == (intptr_t)queue_window);
            queue_seen++;
        }
    }
    return rc;
}

static void drain(void) {
    for (int i = 0; i < 30; i++) { poll_no_drop(); usleep(1000); }
}

static void resize_window(intptr_t window, int w, int h) {
    CHECK(zan_gui_resize_client(window, w, h) == 0);
    int event_seen = 0;
    for (int i = 0; i < 500; i++) {
        if (zan_gui_poll_event() == 0 && zan_gui_event_kind() == 7 &&
            zan_gui_event_hwnd() == window &&
            zan_gui_event_x() == w && zan_gui_event_y() == h) {
            event_seen = 1;
            break;
        }
        usleep(1000);
    }
    CHECK(event_seen);
    CHECK(zan_gui_client_width(window) == w);
    CHECK(zan_gui_client_height(window) == h);
    Window root;
    int x, y;
    unsigned width, height, border, depth;
    CHECK(XGetGeometry(display, (Window)window, &root, &x, &y,
                       &width, &height, &border, &depth));
    CHECK(width == (unsigned)w && height == (unsigned)h);
}

static void send_message(Window target, const char *type, long a, long b,
                         long c, long d, long e) {
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = target;
    event.xclient.message_type = atom(type);
    event.xclient.format = 32;
    event.xclient.data.l[0] = a;
    event.xclient.data.l[1] = b;
    event.xclient.data.l[2] = c;
    event.xclient.data.l[3] = d;
    event.xclient.data.l[4] = e;
    CHECK(XSendEvent(display, target, False, NoEventMask, &event));
    XFlush(display);
}

static void test_messages(intptr_t window) {
    send_message((Window)window, "ZAN_TEST_UNRELATED", (long)atom("WM_DELETE_WINDOW"), 0, 0, 0, 0);
    send_message((Window)window, "WM_PROTOCOLS", (long)atom("ZAN_TEST_UNRELATED"), 0, 0, 0, 0);
    drain();
    send_message((Window)window, "WM_PROTOCOLS", (long)atom("WM_DELETE_WINDOW"), 0, 0, 0, 0);
    int closed = 0;
    for (int i = 0; i < 500; i++) {
        if (zan_gui_poll_event() == 0 && zan_gui_event_kind() == 8) {
            CHECK(zan_gui_event_hwnd() == window);
            closed = 1;
            break;
        }
        usleep(1000);
    }
    CHECK(closed);
}

typedef struct { Window window; Atom type; } message_match_t;

static Bool match_message(Display *d, XEvent *event, XPointer arg) {
    (void)d;
    message_match_t *match = (message_match_t *)arg;
    return event->type == ClientMessage && event->xclient.window == match->window &&
        event->xclient.message_type == match->type;
}

static XClientMessageEvent wait_message(Window source, const char *type, int pump) {
    message_match_t match = { source, atom(type) };
    for (int i = 0; i < 500; i++) {
        XEvent event;
        if (XCheckIfEvent(display, &event, match_message, (XPointer)&match))
            return event.xclient;
        if (pump) poll_no_drop();
        usleep(1000);
    }
    fprintf(stderr, "timed out waiting for %s\n", type);
    CHECK(0);
    XClientMessageEvent empty = { 0 };
    return empty;
}

static void check_finished(Window source, Window target, int accepted, int pump) {
    XClientMessageEvent event = wait_message(source, "XdndFinished", pump);
    CHECK((Window)event.data.l[0] == target);
    CHECK(event.data.l[1] == accepted);
    CHECK((Atom)event.data.l[2] == (accepted ? atom("XdndActionCopy") : None));
}

static void begin_drop(Window source, Window target, int type_list,
                       const char *action, int accepted) {
    drop_sequence++;
    /* Creation/centering is queued on the driver's connection; map before measuring. */
    CHECK(zan_gui_show_window((intptr_t)target) == 0);
    XWindowAttributes attributes;
    int viewable = 0;
    for (int i = 0; i < 500 && !viewable; i++) {
        CHECK(XGetWindowAttributes(display, target, &attributes));
        viewable = attributes.map_state == IsViewable;
        if (!viewable) { poll_no_drop(); usleep(1000); }
    }
    CHECK(viewable);
    XSetSelectionOwner(display, atom("XdndSelection"), source, CurrentTime);
    if (type_list) {
        Atom types[] = { atom("text/plain"), atom("text/uri-list"), atom("application/octet-stream") };
        XChangeProperty(display, source, atom("XdndTypeList"), XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)types, 3);
    }
    send_message(target, "XdndEnter", (long)source, (5L << 24) | !!type_list,
                 (long)atom("text/uri-list"), 0, 0);
    Window child;
    int x, y;
    CHECK(XTranslateCoordinates(display, target, DefaultRootWindow(display),
                                 27, 43, &x, &y, &child));
    long packed = (long)(((uint32_t)(uint16_t)x << 16) | (uint16_t)y);
    send_message(target, "XdndPosition", (long)source, 0, packed, CurrentTime, (long)atom(action));
    XClientMessageEvent status = wait_message(source, "XdndStatus", 1);
    CHECK((Window)status.data.l[0] == target);
    if ((status.data.l[1] & 1) != accepted) {
        int client_x, client_y;
        CHECK(XTranslateCoordinates(display, DefaultRootWindow(display), target,
                                     x, y, &client_x, &client_y, &child));
        fprintf(stderr, "XdndStatus stage=%s gesture=%u source=%lu target=%lu "
                "types=%s action=%s flags=%ld expected=%d root=(%d,%d) "
                "client=(%d,%d) size=(%d,%d) map=%d\n",
                test_stage, drop_sequence, source, target, type_list ? "property" : "inline",
                action, status.data.l[1], accepted, x, y, client_x, client_y,
                attributes.width, attributes.height, attributes.map_state);
    }
    CHECK((status.data.l[1] & 1) == accepted);
    CHECK((Atom)status.data.l[4] == (accepted ? atom("XdndActionCopy") : None));
    send_message(target, "XdndDrop", (long)source, 0, CurrentTime, 0, 0);
}

static XSelectionRequestEvent wait_request(Window source) {
    for (int i = 0; i < 500; i++) {
        XEvent event;
        if (XCheckTypedWindowEvent(display, source, SelectionRequest, &event)) {
            CHECK(event.xselectionrequest.owner == source);
            CHECK(event.xselectionrequest.selection == atom("XdndSelection"));
            CHECK(event.xselectionrequest.target == atom("text/uri-list"));
            CHECK(event.xselectionrequest.property != None);
            return event.xselectionrequest;
        }
        poll_no_drop();
        usleep(1000);
    }
    CHECK(0);
    XSelectionRequestEvent empty = { 0 };
    return empty;
}

static void notify_selection(const XSelectionRequestEvent *request, Window destination,
                             Atom selection, Atom property) {
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xselection.type = SelectionNotify;
    event.xselection.requestor = request->requestor;
    event.xselection.selection = selection;
    event.xselection.target = request->target;
    event.xselection.property = property;
    event.xselection.time = request->time;
    CHECK(XSendEvent(display, destination, False, NoEventMask, &event));
    XFlush(display);
}

static void reply_selection(const XSelectionRequestEvent *request, const char *data,
                             size_t length, Atom type) {
    if (data)
        XChangeProperty(display, request->requestor, request->property, type, 8,
                        PropModeReplace, (const unsigned char *)data, (int)length);
    notify_selection(request, request->requestor, request->selection,
                     data ? request->property : None);
}

static void wait_drop(Window target) {
    for (int i = 0; i < 500; i++) {
        int rc = zan_gui_poll_event();
        CHECK(rc >= 0);
        if (rc == 0) {
            CHECK(zan_gui_event_kind() != 8);
            if (zan_gui_drop_pending()) {
                CHECK(zan_gui_event_kind() == 9);
                CHECK(zan_gui_event_hwnd() == (intptr_t)target);
                CHECK(zan_gui_event_x() == 27 && zan_gui_event_y() == 43);
                return;
            }
        }
        usleep(1000);
    }
    CHECK(0);
}

static void take_path(const char *path, int remaining) {
    CHECK(zan_gui_drop_pending());
    CHECK(strcmp(zan_gui_drop_take(), path) == 0);
    CHECK(zan_gui_drop_pending() == remaining);
}

static void test_uri_lists(Window source, Window target) {
    test_stage = "URI lists";
    const char uris[] = "# file manager selection\r\n\r\n"
        "file:///tmp/%E4%B8%AD%E6%96%87%20one.txt\r\n"
        "file://localhost/tmp/原文%20two.txt\n"
        "FILE:/tmp/hash%23query%3Fplus+percent%25.txt";
    begin_drop(source, target, 1, "XdndActionCopy", 1);
    XSelectionRequestEvent request = wait_request(source);
    reply_selection(&request, uris, sizeof(uris) - 1, atom("text/uri-list"));
    wait_drop(target);
    CHECK(zan_gui_drop_pending() == 1); /* pending is a Boolean, not a path count. */
    take_path("/tmp/中文 one.txt", 1);
    take_path("/tmp/原文 two.txt", 1);
    take_path("/tmp/hash#query?plus+percent%.txt", 0);
    CHECK(strcmp(zan_gui_drop_take(), "") == 0);
    check_finished(source, target, 1, 0);
    drain();

    const char *bad[] = {
        "file://remote.example/tmp/a\r\n", "file://localhost:22/tmp/a\n",
        "file://user@localhost/tmp/a\n", "file://%6cocalhost/tmp/a\n",
        "https://localhost/tmp/a\n", "file:relative\n", "file:///tmp/a%00b\n",
        "file:///tmp/a%2\n", "file:///tmp/a%ZZ\n", "file:///tmp/%C0%AF\n",
        "file:///tmp/%ED%A0%80\n", "file:///tmp/%F4%90%80%80\n",
        "file:///tmp/%E4\n", "file:///tmp/a?query\n", "file:///tmp/a#fragment\n",
        "file:///tmp/good\nfile://remote.example/tmp/bad\n", "# only a comment\r\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        begin_drop(source, target, 0, "XdndActionCopy", 1);
        request = wait_request(source);
        reply_selection(&request, bad[i], strlen(bad[i]), atom("text/uri-list"));
        check_finished(source, target, 0, 1);
        CHECK(!zan_gui_drop_pending());
    }
    const char nul_uri[] = "file:///tmp/a\0b\n";
    begin_drop(source, target, 0, "XdndActionCopy", 1);
    request = wait_request(source);
    reply_selection(&request, nul_uri, sizeof(nul_uri) - 1, atom("text/uri-list"));
    check_finished(source, target, 0, 1);
    begin_drop(source, target, 0, "XdndActionMove", 0);
    check_finished(source, target, 0, 1);
}

static int requestor_exists(Display *d, Window window);

static void test_selection_boundaries(Window source, Window other, Window first, Window second) {
    test_stage = "selection boundaries";
    const char first_data[] = "file:///tmp/first-one\nfile:///tmp/first-two\n";
    const char second_data[] = "file:///tmp/second\n";
    begin_drop(source, first, 0, "XdndActionCopy", 1);
    XSelectionRequestEvent a = wait_request(source);
    begin_drop(source, second, 0, "XdndActionCopy", 1);
    XSelectionRequestEvent b = wait_request(source);
    CHECK(a.requestor != b.requestor);
    reply_selection(&b, second_data, sizeof(second_data) - 1, atom("text/uri-list"));
    reply_selection(&a, first_data, sizeof(first_data) - 1, atom("text/uri-list"));
    wait_drop(second);
    take_path("/tmp/second", 0);
    check_finished(source, second, 1, 0);
    wait_drop(first);
    take_path("/tmp/first-one", 1);
    check_finished(source, first, 1, 0);
    XEvent blur;
    memset(&blur, 0, sizeof(blur));
    blur.xfocus.type = FocusOut;
    blur.xfocus.window = second;
    blur.xfocus.mode = NotifyNormal;
    blur.xfocus.detail = NotifyNonlinear;
    CHECK(XSendEvent(display, second, False, FocusChangeMask, &blur));
    XFlush(display);
    int blurred = 0;
    for (int i = 0; i < 500 && !blurred; i++) {
        if (zan_gui_poll_event() == 0) {
            CHECK(!zan_gui_drop_pending());
            blurred = zan_gui_event_kind() == 9 && zan_gui_event_hwnd() == (intptr_t)second;
        }
        usleep(1000);
    }
    CHECK(blurred); /* Blur must never expose the preceding batch. */
    CHECK(strcmp(zan_gui_drop_take(), "") == 0);

    begin_drop(source, first, 0, "XdndActionCopy", 1);
    XSelectionRequestEvent current = wait_request(source);
    CHECK(current.requestor != a.requestor && current.requestor != b.requestor);
    /* Late replies delivered to a live window still carry their retired requestor. */
    notify_selection(&a, first, a.selection, a.property);
    notify_selection(&current, first, atom("CLIPBOARD"), current.property);
    send_message(first, "XdndEnter", (long)other, 5L << 24,
                 (long)atom("text/uri-list"), 0, 0);
    send_message(first, "XdndLeave", (long)source, 0, 0, 0, 0);
    send_message(first, "XdndDrop", (long)source, 0, CurrentTime, 0, 0);
    drain();
    reply_selection(&current, second_data, sizeof(second_data) - 1, atom("text/uri-list"));
    wait_drop(first);
    take_path("/tmp/second", 0);
    check_finished(source, first, 1, 0);
    drain();

    begin_drop(source, first, 0, "XdndActionCopy", 1);
    current = wait_request(source);
    XSetSelectionOwner(display, atom("XdndSelection"), other, CurrentTime);
    reply_selection(&current, second_data, sizeof(second_data) - 1, atom("text/uri-list"));
    check_finished(source, first, 0, 1);

    begin_drop(source, first, 0, "XdndActionCopy", 1);
    current = wait_request(source);
    reply_selection(&current, NULL, 0, None);
    check_finished(source, first, 0, 1);
    begin_drop(source, first, 0, "XdndActionCopy", 1);
    current = wait_request(source);
    unsigned long transfer_size = sizeof(second_data) - 1;
    XChangeProperty(display, current.requestor, current.property, atom("INCR"), 32,
                    PropModeReplace, (unsigned char *)&transfer_size, 1);
    notify_selection(&current, current.requestor, current.selection, current.property);
    check_finished(source, first, 0, 1);
    CHECK(!requestor_exists(display, current.requestor)); /* INCR is rejected, never partially queued. */
    CHECK(!zan_gui_drop_pending());

    begin_drop(source, first, 0, "XdndActionCopy", 1);
    current = wait_request(source);
    reply_selection(&current, second_data, sizeof(second_data) - 1, atom("UTF8_STRING"));
    check_finished(source, first, 0, 1);

    Window gone = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    XDestroyWindow(display, gone);
    XSync(display, False);
    send_message(first, "XdndEnter", (long)gone, (5L << 24) | 1, 0, 0, 0);
    send_message(first, "XdndPosition", (long)gone, 0, 0, CurrentTime, (long)atom("XdndActionCopy"));
    drain(); /* A departed external source must not terminate the process on BadWindow. */
}

static void test_queue_boundaries(Window source, Window first, Window second) {
    test_stage = "queue boundaries";
    const char data[] = "file:///tmp/queued-one\nfile:///tmp/queued-two\n";
    queue_window = second;
    queue_seen = 0;
    begin_drop(source, first, 0, "XdndActionCopy", 1);
    XSelectionRequestEvent request = wait_request(source);
    queue_fill = 63;
    reply_selection(&request, data, sizeof(data) - 1, atom("text/uri-list"));
    check_finished(source, first, 0, 1);
    drain();
    for (int i = 0; i < 100 && zan_gui_inject_pending(); i++) poll_no_drop();
    CHECK(queue_fill == 0 && queue_seen == 63 && zan_gui_inject_pending() == 0);
    CHECK(!zan_gui_drop_pending());

    intptr_t doomed = zan_gui_create_window("queued doomed", 80, 80);
    CHECK(doomed);
    begin_drop(source, (Window)doomed, 0, "XdndActionCopy", 1);
    request = wait_request(source);
    queue_fill = 1;
    reply_selection(&request, data, sizeof(data) - 1, atom("text/uri-list"));
    for (int i = 0; i < 500 && queue_fill; i++) { poll_no_drop(); usleep(1000); }
    CHECK(!queue_fill && zan_gui_inject_pending() == 1);
    check_finished(source, (Window)doomed, 1, 0);
    CHECK(zan_gui_destroy_window(doomed) == 0);
    CHECK(zan_gui_poll_event() == 0);
    CHECK(zan_gui_event_kind() == 0 && !zan_gui_drop_pending());
    drain();

    doomed = zan_gui_create_window("active doomed", 80, 80);
    CHECK(doomed);
    begin_drop(source, (Window)doomed, 0, "XdndActionCopy", 1);
    request = wait_request(source);
    reply_selection(&request, data, sizeof(data) - 1, atom("text/uri-list"));
    wait_drop((Window)doomed);
    take_path("/tmp/queued-one", 1);
    check_finished(source, (Window)doomed, 1, 0);
    CHECK(zan_gui_destroy_window(doomed) == 0);
    CHECK(!zan_gui_drop_pending() && strcmp(zan_gui_drop_take(), "") == 0);
    drain();

    doomed = zan_gui_create_window("pending doomed", 80, 80);
    CHECK(doomed);
    begin_drop(source, (Window)doomed, 0, "XdndActionCopy", 1);
    request = wait_request(source);
    CHECK(zan_gui_destroy_window(doomed) == 0);
    check_finished(source, (Window)doomed, 0, 0);
    notify_selection(&request, second, request.selection, request.property);
    drain();
    CHECK(!zan_gui_drop_pending());
}

static int expected_bad_window;
static int requestor_error(Display *d, XErrorEvent *error) {
    (void)d;
    CHECK(error->error_code == BadWindow);
    expected_bad_window++;
    return 0;
}

static int requestor_exists(Display *d, Window window) {
    Window root, parent, *children = NULL;
    unsigned count = 0;
    expected_bad_window = 0;
    XErrorHandler previous = XSetErrorHandler(requestor_error);
    int exists = XQueryTree(d, window, &root, &parent, &children, &count);
    XSync(d, False);
    XSetErrorHandler(previous);
    if (children) XFree(children);
    CHECK(exists ? expected_bad_window == 0 : expected_bad_window == 1);
    return exists;
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void check_timeout_event(int rc) {
    CHECK(rc >= 0);
    if (rc == 0) CHECK(zan_gui_event_kind() != 8);
    CHECK(!zan_gui_drop_pending());
}

static void test_selection_timeout(Window source, Window first, Window second) {
    test_stage = "selection timeout";
    const char data[] = "file:///tmp/after-timeout\n";
    for (int mode = 0; mode < 3; mode++) {
        begin_drop(source, first, 0, "XdndActionCopy", 1);
        XSelectionRequestEvent stale = wait_request(source);
        CHECK(requestor_exists(display, stale.requestor));
        drain();
        int64_t start = monotonic_ms();
        XSelectionRequestEvent survivor = { 0 };
        if (mode == 2) {
            /* A separate client observes cleanup before supplying a wake-up event. */
            pid_t child = fork();
            CHECK(child >= 0);
            if (child == 0) {
                Display *observer = XOpenDisplay(NULL);
                if (!observer) _exit(2);
                int retired = 0;
                for (int i = 0; i < 750 && !retired; i++) {
                    retired = !requestor_exists(observer, stale.requestor);
                    if (!retired) usleep(2000);
                }
                XEvent event;
                memset(&event, 0, sizeof(event));
                event.xmotion.type = MotionNotify;
                event.xmotion.window = second;
                event.xmotion.x = 101;
                event.xmotion.y = 102;
                XSendEvent(observer, second, False, PointerMotionMask, &event);
                XSync(observer, False);
                XCloseDisplay(observer);
                _exit(retired ? 0 : 3);
            }
            int woke = 0;
            while (!woke && monotonic_ms() - start < 2500) {
                check_timeout_event(zan_gui_wait_event());
                woke = zan_gui_event_kind() == 1 && zan_gui_event_hwnd() == (intptr_t)second &&
                    zan_gui_event_x() == 101 && zan_gui_event_y() == 102;
            }
            CHECK(woke);
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        } else {
            message_match_t match = { source, atom("XdndFinished") };
            int finished = 0;
            while (!finished && monotonic_ms() - start < 2000) {
                if (mode == 0 && !survivor.requestor && monotonic_ms() - start >= 600) {
                    begin_drop(source, second, 0, "XdndActionCopy", 1);
                    survivor = wait_request(source);
                }
                if (mode == 0) { poll_no_drop(); usleep(1000); }
                else check_timeout_event(zan_gui_wait_event_timeout(1100));
                XEvent event;
                if (XCheckIfEvent(display, &event, match_message, (XPointer)&match)) {
                    CHECK((Window)event.xclient.data.l[0] == first);
                    CHECK(event.xclient.data.l[1] == 0 && event.xclient.data.l[2] == None);
                    finished = 1;
                }
            }
            CHECK(finished);
        }
        if (mode == 2) check_finished(source, first, 0, 0);
        CHECK(!requestor_exists(display, stale.requestor));
        CHECK(!zan_gui_drop_pending());
        CHECK(monotonic_ms() - start < 2500);
        if (mode == 0) {
            CHECK(survivor.requestor && requestor_exists(display, survivor.requestor));
            reply_selection(&survivor, data, sizeof(data) - 1, atom("text/uri-list"));
            wait_drop(second);
            take_path("/tmp/after-timeout", 0);
            check_finished(source, second, 1, 0);
            drain();
        }
        begin_drop(source, first, 0, "XdndActionCopy", 1);
        XSelectionRequestEvent current = wait_request(source);
        CHECK(current.requestor != stale.requestor);
        notify_selection(&stale, first, stale.selection, stale.property);
        drain();
        CHECK(requestor_exists(display, current.requestor));
        reply_selection(&current, data, sizeof(data) - 1, atom("text/uri-list"));
        wait_drop(first);
        take_path("/tmp/after-timeout", 0);
        check_finished(source, first, 1, 0);
        drain();
    }
}

static void focus_window(Window window) {
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
    drain(); /* XSync alone never dispatches the driver's FocusIn/FocusOut. */
}

static void test_key(Window window, int mode, int chars, int calls, test_ic_t *expected) {
    lookup_mode = mode;
    lookup_calls = 0;
    lookup_ic = NULL;
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xkey.type = KeyPress;
    event.xkey.display = display;
    event.xkey.window = window;
    event.xkey.root = DefaultRootWindow(display);
    event.xkey.keycode = XKeysymToKeycode(display, XK_a);
    event.xkey.same_screen = True;
    CHECK(XSendEvent(display, window, False, KeyPressMask, &event));
    XFlush(display);
    int keys = 0, seen = 0;
    for (int i = 0; i < 100; i++) {
        int rc = zan_gui_poll_event();
        CHECK(rc >= 0);
        if (rc == 0 && (zan_gui_event_kind() == 4 || zan_gui_event_kind() == 6)) {
            CHECK(zan_gui_event_hwnd() == (intptr_t)window);
            if (zan_gui_event_kind() == 4) keys++;
            else {
                int code = zan_gui_event_keycode();
                CHECK(mode == 1);
                CHECK(code == (seen < 24 ? 0x4e2d : seen == 24 ? ' ' : 'z'));
                seen++;
            }
        }
        usleep(1000);
    }
    CHECK(keys == (mode == 3 ? 0 : 1));
    CHECK(seen == chars && lookup_calls == calls && lookup_ic == (XIC)expected);
}

static void test_injected_im(void) {
    inject_im = 1;
    intptr_t first = zan_gui_create_window("injected IM first", 180, 120);
    intptr_t second = zan_gui_create_window("injected IM second", 180, 120);
    CHECK(first && second && fake_ic_count == 2);
    XWindowAttributes attributes;
    CHECK(XGetWindowAttributes(display, (Window)first, &attributes));
    CHECK(attributes.all_event_masks & PropertyChangeMask);
    CHECK(zan_gui_show_window(first) == 0 && zan_gui_show_window(second) == 0);
    focus_window((Window)first);
    CHECK(fake_ics[0].focused && !fake_ics[1].focused && fake_ics[0].set_count > 0);
    zan_gui_set_ime_pos(25, 45);
    CHECK(fake_ics[0].spot_count == 1 && fake_ics[0].spot.x == 25 && fake_ics[0].spot.y == 45);
    CHECK(fake_ics[1].spot_count == 0);
    focus_window((Window)second);
    CHECK(!fake_ics[0].focused && fake_ics[1].focused && fake_ics[0].unset_count > 0);
    zan_gui_set_ime_pos(INT32_MIN, INT32_MAX);
    CHECK(fake_ics[1].spot_count == 1 && fake_ics[1].spot.x == -32768 && fake_ics[1].spot.y == 32767);
    CHECK(fake_ics[0].spot_count == 1);
    test_key((Window)second, 1, 26, 2, &fake_ics[1]);
    test_key((Window)second, 2, 0, 2, &fake_ics[1]);
    test_key((Window)second, 3, 0, 1, &fake_ics[1]);
    test_key((Window)second, 4, 0, 1, &fake_ics[1]);
    test_key((Window)second, 5, 0, 1, &fake_ics[1]);
    CHECK(zan_gui_destroy_window(first) == 0 && fake_ics[0].destroyed);
    zan_gui_set_ime_pos(31, 52);
    CHECK(fake_ics[1].spot_count == 2 && fake_ics[1].spot.x == 31 && fake_ics[1].spot.y == 52);
    focus_window(PointerRoot);
    zan_gui_set_ime_pos(1, 2);
    CHECK(fake_ics[1].spot_count == 2);
    CHECK(zan_gui_destroy_window(second) == 0 && fake_ics[1].destroyed);
    drain();

    inject_position = 0;
    intptr_t fallback = zan_gui_create_window("injected no-position IM", 100, 100);
    CHECK(fallback && fake_ic_count == 3);
    CHECK(zan_gui_show_window(fallback) == 0);
    focus_window((Window)fallback);
    zan_gui_set_ime_pos(10, 20);
    CHECK(fake_ics[2].spot_count == 0);
    CHECK(zan_gui_destroy_window(fallback) == 0 && fake_ics[2].destroyed);
    fail_create_ic = 1;
    intptr_t no_ic = zan_gui_create_window("injected unavailable IC", 100, 100);
    CHECK(no_ic && fake_ic_count == 3);
    CHECK(zan_gui_show_window(no_ic) == 0);
    focus_window((Window)no_ic);
    zan_gui_set_ime_pos(10, 20);
    CHECK(zan_gui_destroy_window(no_ic) == 0);
    inject_im = 0;
    puts("injected XIC: spot/focus/filter-mask/overflow/fallback boundaries ok");
}

int main(void) {
    setlocale(LC_ALL, "");
    intptr_t first = zan_gui_create_window("platform first", 240, 160);
    intptr_t second = zan_gui_create_window("platform second", 260, 180);
    CHECK(first && second && first != second);
    display = XOpenDisplay(NULL);
    CHECK(display);
    CHECK(zan_gui_show_window(first) == 0 && zan_gui_show_window(second) == 0);
    drain();
    resize_window(first, 321, 239);
    CHECK(zan_gui_client_width(second) == 260 && zan_gui_client_height(second) == 180);
    resize_window(second, 483, 361);
    CHECK(zan_gui_client_width(first) == 321 && zan_gui_client_height(first) == 239);
    CHECK(zan_gui_resize_client(first, 0, 100) == -1);
    CHECK(zan_gui_resize_client(first, 100, -1) == -1);
    CHECK(zan_gui_resize_client(first, 65536, 100) == -1);
    CHECK(zan_gui_resize_client(first, 100, 65536) == -1);
    CHECK(zan_gui_resize_client(0, 100, 100) == -1);
    CHECK(zan_gui_resize_client((intptr_t)-1, 100, 100) == -1);
    focus_window((Window)second);
    zan_gui_set_ime_pos(25, 45);
    zan_gui_set_ime_pos(INT32_MIN, INT32_MAX);
    printf("system XIM: queries=%d, Position style=%s, Position IC=%s%s\n",
           system_style_queries, system_position_style ? "offered" : "unavailable",
           system_position_created ? "created" : "unavailable",
           system_position_created ? "; candidate UI not visually verified" : "; real spot positioning not verified");
    test_messages(second);
    Window sender = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    Window other = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    CHECK(sender && other);
    test_uri_lists(sender, (Window)first);
    test_selection_boundaries(sender, other, (Window)first, (Window)second);
    test_queue_boundaries(sender, (Window)first, (Window)second);
    test_selection_timeout(sender, (Window)first, (Window)second);
    CHECK(zan_gui_destroy_window(first) == 0);
    CHECK(zan_gui_resize_client(first, 100, 100) == -1);
    CHECK(zan_gui_destroy_window(second) == 0);
    drain();
    test_injected_im();
    XDestroyWindow(display, sender);
    XDestroyWindow(display, other);
    XCloseDisplay(display);
    puts("x11 platform ok: real Xdnd protocol, URI validation, batches and queue boundaries");
    return 0;
}
