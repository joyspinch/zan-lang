/* Multi-window event routing (X11 backend).
 *
 * One process can now own several top-level windows. Each window keeps its own
 * size, back buffer, GC and input context, and every decoded event carries the
 * handle of the window it came from (queried with zan_gui_event_hwnd). This
 * test creates two differently-sized windows, moves them apart so pointer
 * events are unambiguous, and asserts that:
 *   - per-handle client_width/height return each window's own size;
 *   - a content click routes to the window under the pointer (event_hwnd);
 *   - present() targets a window by handle without error;
 *   - closing one window leaves the other fully usable.
 * Runs headless under Xvfb (no window manager required). */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int64_t i64;

extern i64 zan_gui_create_window(const char *title, i64 width, i64 height);
extern i64 zan_gui_show_window(i64 window);
extern i64 zan_gui_close_window(i64 window);
extern i64 zan_gui_client_width(i64 window);
extern i64 zan_gui_client_height(i64 window);
extern i64 zan_gui_create_surface(i64 width, i64 height);
extern i64 zan_gui_present(i64 window, i64 surface_id);
extern i64 zan_gui_poll_event(void);
extern i64 zan_gui_event_kind(void);
extern i64 zan_gui_event_hwnd(void);

static Display *d;

static void pump(int n) {
    for (int j = 0; j < n; j++) { zan_gui_poll_event(); usleep(3000); }
}

/* Warp to root (rx,ry), click button 1, and return the handle of the window
 * that produced the resulting kind-2 mouse-down (0 if none). */
static i64 click_hwnd(int rx, int ry) {
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, rx, ry);
    XSync(d, False);
    pump(6);
    XTestFakeButtonEvent(d, 1, 1, CurrentTime);
    XSync(d, False);
    i64 hwnd = 0;
    for (int j = 0; j < 12; j++) {
        if (zan_gui_poll_event() == 0 && zan_gui_event_kind() == 2) {
            hwnd = zan_gui_event_hwnd();
            break;
        }
        usleep(3000);
    }
    XTestFakeButtonEvent(d, 1, 0, CurrentTime);
    XSync(d, False);
    pump(6);
    return hwnd;
}

int main(void) {
    i64 w1 = zan_gui_create_window("win-1", 300, 200);
    i64 w2 = zan_gui_create_window("win-2", 360, 260);
    if (!w1 || !w2 || w1 == w2) { printf("create failed\n"); return 1; }
    zan_gui_show_window(w1);
    zan_gui_show_window(w2);

    /* Per-handle sizes must be independent. */
    int sizes_ok = zan_gui_client_width(w1) == 300 && zan_gui_client_height(w1) == 200 &&
                   zan_gui_client_width(w2) == 360 && zan_gui_client_height(w2) == 260;
    printf("per-window sizes: %s\n", sizes_ok ? "OK" : "FAIL");

    /* Present each window by handle (own back buffer, no cross-talk). */
    i64 s1 = zan_gui_create_surface(300, 200);
    i64 s2 = zan_gui_create_surface(360, 260);
    int present_ok = zan_gui_present(w1, s1) == 0 && zan_gui_present(w2, s2) == 0;
    printf("present isolation: %s\n", present_ok ? "OK" : "FAIL");

    d = XOpenDisplay(NULL);
    if (!d) { printf("no display\n"); return 3; }
    /* Separate the two windows so the pointer lands unambiguously in one. */
    XMoveWindow(d, (Window)w1, 0, 0);
    XMoveWindow(d, (Window)w2, 400, 0);
    XRaiseWindow(d, (Window)w1);
    XRaiseWindow(d, (Window)w2);
    XSync(d, False);
    pump(30);

    /* Content clicks (below the 32px title bar, away from resize borders). */
    i64 h1 = click_hwnd(150, 120);
    i64 h2 = click_hwnd(400 + 180, 120);
    int route_ok = (h1 == w1) && (h2 == w2);
    printf("routing: click1->%s click2->%s : %s\n",
           h1 == w1 ? "w1" : "?", h2 == w2 ? "w2" : "?",
           route_ok ? "OK" : "FAIL");

    /* Destroying one window must not disturb the other. */
    zan_gui_close_window(w1);
    pump(10);
    int survive_ok = zan_gui_client_width(w2) == 360 &&
                     zan_gui_client_height(w2) == 260 &&
                     zan_gui_present(w2, s2) == 0;
    i64 h2b = click_hwnd(400 + 180, 120);
    survive_ok = survive_ok && (h2b == w2);
    printf("survivor usable after close: %s\n", survive_ok ? "OK" : "FAIL");

    zan_gui_close_window(w2);

    if (sizes_ok && present_ok && route_ok && survive_ok) {
        printf("multi-window routing ok\n");
        return 0;
    }
    printf("multi-window routing FAILED\n");
    return 5;
}
