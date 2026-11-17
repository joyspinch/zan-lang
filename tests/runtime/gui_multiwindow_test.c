#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

extern intptr_t zan_gui_create_window(const char *title, int32_t width, int32_t height);
extern int32_t zan_gui_show_window(intptr_t window);
extern int32_t zan_gui_close_window(intptr_t window);
extern int32_t zan_gui_client_width(intptr_t window);
extern int32_t zan_gui_client_height(intptr_t window);
extern int32_t zan_gui_create_surface(int32_t width, int32_t height);
extern int32_t zan_gui_present(intptr_t window, int32_t surface_id);
extern int32_t zan_gui_poll_event(void);
extern int32_t zan_gui_event_kind(void);
extern intptr_t zan_gui_event_hwnd(void);

static Display *d;

static void pump(int n) {
    for (int j = 0; j < n; j++) { zan_gui_poll_event(); usleep(3000); }
}

static intptr_t click_hwnd(int rx, int ry) {
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, rx, ry);
    XSync(d, False);
    pump(6);
    XTestFakeButtonEvent(d, 1, 1, CurrentTime);
    XSync(d, False);
    intptr_t hwnd = 0;
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
    intptr_t w1 = zan_gui_create_window("win-1", 300, 200);
    intptr_t w2 = zan_gui_create_window("win-2", 360, 260);
    if (!w1 || !w2 || w1 == w2) { printf("create failed\n"); return 1; }
    zan_gui_show_window(w1);
    zan_gui_show_window(w2);

    int sizes_ok = zan_gui_client_width(w1) == 300 && zan_gui_client_height(w1) == 200 &&
                   zan_gui_client_width(w2) == 360 && zan_gui_client_height(w2) == 260;
    printf("per-window sizes: %s\n", sizes_ok ? "OK" : "FAIL");

    int32_t s1 = zan_gui_create_surface(300, 200);
    int32_t s2 = zan_gui_create_surface(360, 260);
    int present_ok = zan_gui_present(w1, s1) == 0 && zan_gui_present(w2, s2) == 0;
    printf("present isolation: %s\n", present_ok ? "OK" : "FAIL");

    d = XOpenDisplay(NULL);
    if (!d) { printf("no display\n"); return 3; }
    XMoveWindow(d, (Window)w1, 0, 0);
    XMoveWindow(d, (Window)w2, 400, 0);
    XRaiseWindow(d, (Window)w1);
    XRaiseWindow(d, (Window)w2);
    XSync(d, False);
    pump(30);

    intptr_t h1 = click_hwnd(150, 120);
    intptr_t h2 = click_hwnd(400 + 180, 120);
    int route_ok = (h1 == w1) && (h2 == w2);
    printf("routing: click1->%s click2->%s : %s\n",
           h1 == w1 ? "w1" : "?", h2 == w2 ? "w2" : "?",
           route_ok ? "OK" : "FAIL");

    zan_gui_close_window(w1);
    pump(10);
    int survive_ok = zan_gui_client_width(w2) == 360 &&
                     zan_gui_client_height(w2) == 260 &&
                     zan_gui_present(w2, s2) == 0;
    intptr_t h2b = click_hwnd(400 + 180, 120);
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
