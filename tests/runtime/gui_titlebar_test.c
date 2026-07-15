/* Borderless-window caption hit-testing (client-side decorations).
 *
 * The X11 backend draws its own title bar, so a left press on the caption drag
 * area must be delegated to the window manager (interactive move) and NOT
 * surface as a normal mouse-down, whereas presses on window content and on the
 * caption-button cluster must pass through as kind-2 events. This mirrors the
 * Win32 WM_NCHITTEST behavior. Runs headless under Xvfb (no WM is required: the
 * hit-test decides locally whether to consume the press). */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int64_t i64;

extern i64 zan_gui_create_window(const char *title, i64 width, i64 height);
extern i64 zan_gui_show_window(i64 window);
extern i64 zan_gui_set_caption_buttons(i64 count);
extern i64 zan_gui_titlebar_height(void);
extern i64 zan_gui_caption_button_width(void);
extern i64 zan_gui_poll_event(void);
extern i64 zan_gui_event_kind(void);

static Display *d;
static Window target;

/* Warp to (px,py) inside the window, press+release button 1, and report whether
 * a kind-2 (mouse-down) ABI event was produced. */
static int click_produces_mousedown(int px, int py) {
    XWarpPointer(d, None, target, 0, 0, 0, 0, px, py);
    XSync(d, False);
    for (int j = 0; j < 6; j++) { zan_gui_poll_event(); usleep(4000); }
    XTestFakeButtonEvent(d, 1, 1, CurrentTime);
    XSync(d, False);
    int got = 0;
    for (int j = 0; j < 10; j++) {
        if (zan_gui_poll_event() == 0 && zan_gui_event_kind() == 2) got = 1;
        usleep(4000);
    }
    XTestFakeButtonEvent(d, 1, 0, CurrentTime);
    XSync(d, False);
    for (int j = 0; j < 6; j++) { zan_gui_poll_event(); usleep(4000); }
    return got;
}

int main(void) {
    i64 win = zan_gui_create_window("titlebar", 320, 240);
    if (!win) { printf("create failed\n"); return 1; }
    zan_gui_show_window(win);
    zan_gui_set_caption_buttons(5);

    i64 th = zan_gui_titlebar_height();
    i64 bw = zan_gui_caption_button_width();
    printf("titlebar_h=%lld caption_btn_w=%lld\n", (long long)th, (long long)bw);
    if (th < 24 || bw < 24) { printf("metrics too small\n"); return 2; }

    d = XOpenDisplay(NULL);
    if (!d) { printf("no display\n"); return 3; }
    for (int i = 0; i < 40; i++) { zan_gui_poll_event(); usleep(8000); }
    Window rr, parent, *kids = 0; unsigned n = 0;
    XQueryTree(d, DefaultRootWindow(d), &rr, &parent, &kids, &n);
    target = n ? kids[n - 1] : 0;
    if (kids) XFree(kids);
    if (!target) { printf("no window\n"); return 4; }
    XSetInputFocus(d, target, RevertToParent, CurrentTime);
    XSync(d, False);

    int caption = click_produces_mousedown(40, 16);    /* drag area -> consumed */
    int content = click_produces_mousedown(150, 150);  /* content   -> kind2   */
    int button  = click_produces_mousedown(300, 16);   /* btn cluster -> kind2 */

    printf("caption drag consumed: %s\n", caption == 0 ? "OK" : "FAIL");
    printf("content click -> mousedown: %s\n", content == 1 ? "OK" : "FAIL");
    printf("caption-button click -> mousedown: %s\n", button == 1 ? "OK" : "FAIL");

    if (caption == 0 && content == 1 && button == 1) {
        printf("titlebar hit-test ok\n");
        return 0;
    }
    printf("titlebar hit-test FAILED\n");
    return 5;
}
