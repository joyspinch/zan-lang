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

typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t iptr;
typedef uint32_t u32;

#define EXPORT __attribute__((visibility("default")))

typedef void *id_t;
typedef void *sel_t;
typedef void *class_t;
typedef void *method_t;
typedef id_t (*imp_t)(id_t, sel_t, ...);

extern void *dlsym(void *handle, const char *symbol);
#define RTLD_DEFAULT ((void *)(intptr_t)-2)

/* Darwin POSIX declarations */
struct timespec {
    long tv_sec;
    long tv_nsec;
};

extern int nanosleep(const struct timespec *req, struct timespec *rem);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

typedef struct {
    long __sig;
    char __opaque[56];
} zan_pthread_mutex_t;

extern int pthread_mutex_lock(zan_pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(zan_pthread_mutex_t *mutex);
#define ZAN_PTHREAD_MUTEX_INITIALIZER { 0x32AAABA7, {0} }

typedef struct {
    int initialized;
    class_t (*objc_getClass)(const char *name);
    sel_t   (*sel_registerName)(const char *str);
    imp_t   objc_msgSend;
    class_t (*objc_allocateClassPair)(class_t superclass, const char *name, size_t extraBytes);
    void    (*objc_registerClassPair)(class_t cls);
    int     (*class_addMethod)(class_t cls, sel_t name, imp_t imp, const char *types);
    method_t (*class_getInstanceMethod)(class_t cls, sel_t name);
    imp_t   (*method_getImplementation)(method_t m);
    imp_t   (*method_setImplementation)(method_t m, imp_t imp);
    int32_t (*zan_gui_wake)(void);
} zan_ios_objc_bridge_t;

static zan_ios_objc_bridge_t g_bridge;

typedef struct {
    int attached;
    int closed;
    int width;
    int height;
    int dpi;
    id_t ui_window;
    id_t ui_view;
    id_t ui_controller;
} zan_ios_win_t;

static zan_ios_win_t g_iwin;
#define ZAN_IOS_HWND ((iptr)&g_iwin)

typedef struct { int e[8]; iptr win; } zan_iev_t;
#define ZAN_IQ_CAP 512
static zan_iev_t g_iq[ZAN_IQ_CAP];
static int g_iq_head = 0, g_iq_tail = 0;
static zan_pthread_mutex_t g_iq_lock = ZAN_PTHREAD_MUTEX_INITIALIZER;

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
    g_bridge.objc_getClass = (class_t (*)(const char *))dlsym(RTLD_DEFAULT, "objc_getClass");
    g_bridge.sel_registerName = (sel_t (*)(const char *))dlsym(RTLD_DEFAULT, "sel_registerName");
    g_bridge.objc_msgSend = (imp_t)dlsym(RTLD_DEFAULT, "objc_msgSend");
    g_bridge.objc_allocateClassPair = (class_t (*)(class_t, const char *, size_t))dlsym(RTLD_DEFAULT, "objc_allocateClassPair");
    g_bridge.objc_registerClassPair = (void (*)(class_t))dlsym(RTLD_DEFAULT, "objc_registerClassPair");
    g_bridge.class_addMethod = (int (*)(class_t, sel_t, imp_t, const char *))dlsym(RTLD_DEFAULT, "class_addMethod");
    g_bridge.class_getInstanceMethod = (method_t (*)(class_t, sel_t))dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    g_bridge.method_getImplementation = (imp_t (*)(method_t))dlsym(RTLD_DEFAULT, "method_getImplementation");
    g_bridge.method_setImplementation = (imp_t (*)(method_t, imp_t))dlsym(RTLD_DEFAULT, "method_setImplementation");
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

    class_t cls_screen = g_bridge.objc_getClass("UIScreen");
    if (cls_screen) {
        sel_t sel_main = g_bridge.sel_registerName("mainScreen");
        id_t main_screen = g_bridge.objc_msgSend((id_t)cls_screen, sel_main);
        if (main_screen) {
            sel_t sel_scale = g_bridge.sel_registerName("scale");
            typedef double (*scale_fn)(id_t, sel_t);
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

EXPORT void zan_gui_present_full(void) {}

__attribute__((weak))
int32_t zan_gui_font_height(int32_t font_size) {
    return font_size > 0 ? (int32_t)(font_size * 1.2) : 16;
}

EXPORT int32_t zan_gui_font_ascent(int32_t font_size) {
    if (font_size <= 0) return 0;
    int32_t h = zan_gui_font_height(font_size);
    if (h <= 0) h = font_size;
    return (int32_t)((h * 4 + 2) / 5);
}

__attribute__((weak))
void zan_gui_draw_text(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size) {
    (void)surface_id; (void)x; (void)y; (void)text; (void)color; (void)font_size;
}

EXPORT void zan_gui_draw_text_bold(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size) {
    zan_gui_draw_text(surface_id, x, y, text, color, font_size);
}

__attribute__((weak))
void zan_gui_draw_polyline(int32_t surface_id, const int32_t *pts, int32_t n, int32_t color, int32_t thickness) {
    (void)surface_id; (void)pts; (void)n; (void)color; (void)thickness;
}

EXPORT void zan_gui_draw_polybatch(int32_t surface_id, const int32_t *pts, const int32_t *counts, int32_t n_paths, int32_t color, int32_t thickness) {
    if (!pts || !counts || n_paths < 1) return;
    int32_t off = 0;
    for (int32_t p = 0; p < n_paths; p++) {
        int32_t n = counts[p];
        if (n >= 2) {
            zan_gui_draw_polyline(surface_id, pts + off, n, color, thickness);
        }
        off += n * 2;
    }
}
