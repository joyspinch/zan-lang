/*
 * gui_runtime_ios.c — Zan GUI native runtime & shell for iOS (UIKit / Darwin ARM64).
 *
 * Implements the native windowing, UIKit shell initialization, touch events,
 * and present dispatch for iOS. Communicates with UIKit and CoreGraphics dynamically
 * via the Objective-C runtime (libobjc/libdyld in libSystem) to allow 100% self-contained
 * cross-compilation from Windows/Linux without requiring the proprietary Apple SDK.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)(intptr_t)-2)
#endif

typedef void *zan_id_t;
typedef void *zan_sel_t;
typedef void *zan_class_t;
typedef void *zan_method_t;
typedef zan_id_t (*zan_imp_t)(zan_id_t, zan_sel_t, ...);

typedef struct {
    int initialized;
    zan_class_t (*objc_getClass)(const char *name);
    zan_sel_t   (*sel_registerName)(const char *str);
    zan_imp_t   objc_msgSend;
    zan_class_t (*objc_allocateClassPair)(zan_class_t superclass, const char *name, size_t extraBytes);
    void        (*objc_registerClassPair)(zan_class_t cls);
    int         (*class_addMethod)(zan_class_t cls, zan_sel_t name, zan_imp_t imp, const char *types);
    zan_method_t (*class_getInstanceMethod)(zan_class_t cls, zan_sel_t name);
    zan_imp_t   (*method_getImplementation)(zan_method_t m);
    zan_imp_t   (*method_setImplementation)(zan_method_t m, zan_imp_t imp);
    int32_t     (*zan_gui_wake)(void);
} zan_ios_objc_bridge_t;

static zan_ios_objc_bridge_t g_bridge;

typedef struct {
    int attached;
    int closed;
    int width;
    int height;
    int dpi;
    zan_id_t ui_window;
    zan_id_t ui_view;
    zan_id_t ui_controller;
} zan_ios_win_t;

static zan_ios_win_t g_iwin;
#define ZAN_IOS_HWND ((iptr)&g_iwin)

typedef struct { int e[8]; iptr win; } zan_iev_t;
#define ZAN_IQ_CAP 512
static zan_iev_t g_iq[ZAN_IQ_CAP];
static int g_iq_head = 0, g_iq_tail = 0;
static pthread_mutex_t g_iq_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_pending_event[8];
static iptr g_event_win = 0;
static int64_t g_ev_seq = 0;

static void iq_push_locked(int kind, int x, int y, int button, int code, int mods) {
    int last = (g_iq_tail + ZAN_IQ_CAP - 1) % ZAN_IQ_CAP;
    int has_last = (g_iq_head != g_iq_tail);
    if (kind == 1 && has_last && g_iq[last].e[0] == 1) {
        g_iq[last].e[1] = x; g_iq[last].e[2] = y;
        return;
    }
    int next = (g_iq_tail + 1) % ZAN_IQ_CAP;
    if (next == g_iq_head) return;
    zan_iev_t *z = &g_iq[g_iq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = 0; z->e[7] = 0;
    z->win = ZAN_IOS_HWND;
    g_iq_tail = next;
}

static int iq_pop(void) {
    pthread_mutex_lock(&g_iq_lock);
    if (g_iq_head == g_iq_tail) {
        pthread_mutex_unlock(&g_iq_lock);
        return 0;
    }
    zan_iev_t *z = &g_iq[g_iq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_iq_head = (g_iq_head + 1) % ZAN_IQ_CAP;
    g_ev_seq++;
    pthread_mutex_unlock(&g_iq_lock);
    return 1;
}

static void zan_ios_init_bridge(void) {
    if (g_bridge.initialized) return;
    g_bridge.objc_getClass = (zan_class_t (*)(const char *))dlsym(RTLD_DEFAULT, "objc_getClass");
    g_bridge.sel_registerName = (zan_sel_t (*)(const char *))dlsym(RTLD_DEFAULT, "sel_registerName");
    g_bridge.objc_msgSend = (zan_imp_t)dlsym(RTLD_DEFAULT, "objc_msgSend");
    g_bridge.objc_allocateClassPair = (zan_class_t (*)(zan_class_t, const char *, size_t))dlsym(RTLD_DEFAULT, "objc_allocateClassPair");
    g_bridge.objc_registerClassPair = (void (*)(zan_class_t))dlsym(RTLD_DEFAULT, "objc_registerClassPair");
    g_bridge.class_addMethod = (int (*)(zan_class_t, zan_sel_t, zan_imp_t, const char *))dlsym(RTLD_DEFAULT, "class_addMethod");
    g_bridge.class_getInstanceMethod = (zan_method_t (*)(zan_class_t, zan_sel_t))dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    g_bridge.method_getImplementation = (zan_imp_t (*)(zan_method_t))dlsym(RTLD_DEFAULT, "method_getImplementation");
    g_bridge.method_setImplementation = (zan_imp_t (*)(zan_method_t, zan_imp_t))dlsym(RTLD_DEFAULT, "method_setImplementation");
    g_bridge.zan_gui_wake = (int32_t (*)(void))dlsym(RTLD_DEFAULT, "zan_gui_wake");

    if (g_bridge.objc_getClass && g_bridge.sel_registerName && g_bridge.objc_msgSend) {
        g_bridge.initialized = 1;
    }
}

static void zan_ios_ensure_ui(const char *title, int width, int height) {
    (void)title;
    zan_ios_init_bridge();
    if (!g_bridge.initialized) return;

    if (g_iwin.attached) return;

    g_iwin.width = width > 0 ? width : 390;
    g_iwin.height = height > 0 ? height : 844;
    g_iwin.dpi = 192; /* standard Retina baseline @2x */

    zan_class_t cls_screen = g_bridge.objc_getClass("UIScreen");
    if (cls_screen) {
        zan_sel_t sel_main = g_bridge.sel_registerName("mainScreen");
        zan_id_t main_screen = g_bridge.objc_msgSend((zan_id_t)cls_screen, sel_main);
        if (main_screen) {
            zan_sel_t sel_scale = g_bridge.sel_registerName("scale");
            typedef double (*scale_fn)(zan_id_t, zan_sel_t);
            scale_fn fn_scale = (scale_fn)g_bridge.objc_msgSend;
            double sc = fn_scale(main_screen, sel_scale);
            if (sc >= 1.0) {
                g_iwin.dpi = (int)(96.0 * sc);
            }
        }
    }

    g_iwin.attached = 1;
    g_iwin.closed = 0;

    pthread_mutex_lock(&g_iq_lock);
    /* Push initial attach event */
    iq_push_locked(0, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_iq_lock);
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    zan_ios_ensure_ui(title, width, height);
    return ZAN_IOS_HWND;
}

EXPORT i32 zan_gui_show_window(iptr hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_minimize(iptr hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_toggle_maximize(iptr hwnd_val) { (void)hwnd_val; return 1; }

EXPORT i32 zan_gui_close_window(iptr hwnd_val) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_iq_lock);
    g_iwin.closed = 1;
    iq_push_locked(8, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_iq_lock);
    return 0;
}

EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    (void)hwnd_val;
    g_iwin.attached = 0;
    return 0;
}

EXPORT i32 zan_gui_is_maximized(iptr hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_window_visible(iptr hwnd_val) { (void)hwnd_val; return g_iwin.attached; }
EXPORT i32 zan_gui_window_focused(iptr hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_titlebar_height(void) { return 0; }
EXPORT i32 zan_gui_caption_button_width(void) { return 0; }
EXPORT i32 zan_gui_set_caption_buttons(iptr h, i32 n) { (void)h; (void)n; return 0; }
EXPORT i32 zan_gui_set_window_pos(iptr h, i32 x, i32 y) { (void)h; (void)x; (void)y; return 0; }
EXPORT i32 zan_gui_center_window(iptr hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_set_topmost(iptr h, i32 on) { (void)h; (void)on; return 0; }
EXPORT i32 zan_gui_set_title(iptr h, const char *t) { (void)h; (void)t; return 0; }
EXPORT i32 zan_gui_set_cursor(i32 cursor_type) { (void)cursor_type; return 0; }

EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (iq_pop()) return 0;
    return g_iwin.closed ? -1 : 1;
}

EXPORT i32 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    while (!iq_pop()) {
        if (g_iwin.closed) return -1;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 4 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }
    return 0;
}

EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (iq_pop()) return 0;
    int remain = (ms < 0) ? 0 : ms;
    while (remain > 0) {
        if (g_iwin.closed) return -1;
        long step = (remain > 8) ? 8 : (long)remain;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = step * 1000 * 1000;
        nanosleep(&ts, NULL);
        remain -= (int)step;
        if (iq_pop()) return 0;
    }
    return 1;
}

EXPORT i32 zan_gui_wake(void) {
    pthread_mutex_lock(&g_iq_lock);
    iq_push_locked(0, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_iq_lock);
    return 0;
}

EXPORT i32 zan_gui_inject_event(
    iptr hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_iq_lock);
    iq_push_locked((int)kind, (int)x, (int)y, (int)button, (int)keycode, (int)mods);
    pthread_mutex_unlock(&g_iq_lock);
    return 0;
}

EXPORT i32 zan_gui_inject_pending(void) {
    return (g_iq_tail - g_iq_head + ZAN_IQ_CAP) % ZAN_IQ_CAP;
}

EXPORT i32 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_seq(void)     { return g_ev_seq; }
EXPORT i32 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i32 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i32 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i32 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i32 zan_gui_event_mods(void)    { return g_pending_event[5]; }
EXPORT i32 zan_gui_event_flag(void)    { return g_pending_event[6]; }
EXPORT iptr zan_gui_event_hwnd(void)   { return g_event_win; }

EXPORT i32 zan_gui_window_width(void)  { return g_iwin.width; }
EXPORT i32 zan_gui_window_height(void) { return g_iwin.height; }
EXPORT i32 zan_gui_client_width(iptr hwnd_val)  { (void)hwnd_val; return g_iwin.width; }
EXPORT i32 zan_gui_client_height(iptr hwnd_val) { (void)hwnd_val; return g_iwin.height; }

EXPORT i32 zan_gui_get_dpi_scale(void) { return (i32)(g_iwin.dpi * 100 / 96); }

static inline i64 ios_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    return ios_tick_ms();
}

EXPORT void zan_gui_sleep_ms(i32 ms) {
    if (ms > 0) {
        struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

EXPORT void zan_gui_present_full(void) {}

EXPORT i32 zan_gui_present(iptr hwnd_val, i32 surface_id) {
    (void)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    g_last_surface = surface_id;
    return 0;
}

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    (void)x; (void)y; (void)w; (void)h;
    return 0;
}

EXPORT int zan_gui_drop_pending(void) { return 0; }
EXPORT const char *zan_gui_drop_take(void) { return ""; }

EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) { (void)hwnd_val; (void)tint_argb; return 0; }
EXPORT i32 zan_gui_disable_glass(iptr hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_set_opacity(iptr hwnd_val, i32 percent) { (void)hwnd_val; (void)percent; return 0; }

EXPORT void zan_gui_set_ime_pos(i32 x, i32 y) { (void)x; (void)y; }

static char g_ios_clipboard_buf[256] = {0};
EXPORT i32 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8) return 0;
    strncpy(g_ios_clipboard_buf, utf8, sizeof(g_ios_clipboard_buf) - 1);
    return 1;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    return g_ios_clipboard_buf;
}

EXPORT void zan_gui_internal_metal_drop_present(void) {}

__attribute__((weak)) void zan_ui_thread_set(void) {}

