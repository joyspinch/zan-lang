#include <X11/Xatom.h>
#include <X11/Xlib.h>

/* Assertions perform the protocol operations even in Release builds. */
#undef NDEBUG
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern intptr_t zan_gui_create_window(const char *title, int32_t width, int32_t height);
extern int32_t zan_gui_close_window(intptr_t window);
extern int32_t zan_gui_set_cursor(int32_t cursor);
extern int32_t zan_gui_set_clipboard(const char *text);
extern const char *zan_gui_get_clipboard(void);

static void run_clipboard_owner(int ready_fd) {
    Display *display = XOpenDisplay(NULL);
    if (!display) _exit(2);
    Window window =
        XCreateSimpleWindow(display, DefaultRootWindow(display),
                            0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    Atom targets = XInternAtom(display, "TARGETS", False);
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XFlush(display);
    char ready = '1';
    if (write(ready_fd, &ready, 1) != 1) _exit(3);
    close(ready_fd);

    for (;;) {
        struct pollfd pfd = { ConnectionNumber(display), POLLIN, 0 };
        if (poll(&pfd, 1, 5000) <= 0) _exit(4);
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != SelectionRequest) continue;
            XSelectionRequestEvent *req = &event.xselectionrequest;
            XSelectionEvent reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = SelectionNotify;
            reply.display = req->display;
            reply.requestor = req->requestor;
            reply.selection = req->selection;
            reply.target = req->target;
            reply.time = req->time;
            reply.property = req->property ? req->property : req->target;
            if (req->target == utf8 || req->target == XA_STRING) {
                const char text[] = "external clipboard";
                XChangeProperty(display, req->requestor, reply.property,
                                req->target, 8, PropModeReplace,
                                (const unsigned char *)text,
                                (int)strlen(text));
            } else if (req->target == targets) {
                Atom offered[] = { targets, utf8, XA_STRING };
                XChangeProperty(display, req->requestor, reply.property,
                                XA_ATOM, 32, PropModeReplace,
                                (const unsigned char *)offered, 3);
            } else {
                reply.property = None;
            }
            XSendEvent(display, req->requestor, False, 0, (XEvent *)&reply);
            XFlush(display);
        }
    }
}

int main(void) {
    intptr_t window = zan_gui_create_window("x11-test", 64, 64);
    assert(window != 0);
    for (int32_t cursor = 0; cursor <= 5; cursor++)
        assert(zan_gui_set_cursor(cursor) == 0);

    assert(zan_gui_set_clipboard("self clipboard") == 0);
    assert(strcmp(zan_gui_get_clipboard(), "self clipboard") == 0);

    int ready[2];
    assert(pipe(ready) == 0);
    pid_t owner = fork();
    assert(owner >= 0);
    if (owner == 0) {
        close(ready[0]);
        run_clipboard_owner(ready[1]);
    }
    close(ready[1]);
    char byte = 0;
    assert(read(ready[0], &byte, 1) == 1 && byte == '1');
    close(ready[0]);
    assert(strcmp(zan_gui_get_clipboard(), "external clipboard") == 0);

    kill(owner, SIGTERM);
    waitpid(owner, NULL, 0);
    assert(zan_gui_close_window(window) == 0);
    puts("x11 runtime ok");
    return 0;
}
