#include <stdint.h>
#include <stddef.h>

/* Objective-C runtime dynamic bridge for macOS Dock Quit and Event Pump synchronization */
typedef void *id_t;
typedef void *sel_t;
typedef void *class_t;
typedef void *method_t;
typedef id_t (*imp_t)(id_t, sel_t, ...);

extern void *dlsym(void *handle, const char *symbol);
#define RTLD_DEFAULT ((void *)(intptr_t)-2)

typedef struct {
    int initialized;
    class_t (*objc_getClass)(const char *name);
    sel_t (*sel_registerName)(const char *str);
    imp_t objc_msgSend;
    int (*class_addMethod)(class_t cls, sel_t name, imp_t imp, const char *types);
    method_t (*class_getInstanceMethod)(class_t cls, sel_t name);
    imp_t (*method_getImplementation)(method_t m);
    imp_t (*method_setImplementation)(method_t m, imp_t imp);
    int32_t (*zan_gui_inject_event)(intptr_t hwnd, int32_t kind, int32_t x, int32_t y, int32_t button, int32_t keycode, int32_t mods);
    int32_t (*zan_gui_wake)(void);
} mac_objc_bridge_t;

static mac_objc_bridge_t g_bridge;

static void ensure_app_delegate(id_t win_delegate) {
    if (!g_bridge.initialized || !win_delegate) return;
    class_t cls_NSApp = g_bridge.objc_getClass("NSApplication");
    if (!cls_NSApp) return;
    sel_t sel_shared = g_bridge.sel_registerName("sharedApplication");
    id_t nsApp = g_bridge.objc_msgSend((id_t)cls_NSApp, sel_shared);
    if (!nsApp) return;
    sel_t sel_delegate = g_bridge.sel_registerName("delegate");
    id_t cur_del = g_bridge.objc_msgSend(nsApp, sel_delegate);
    if (!cur_del) {
        sel_t sel_setDel = g_bridge.sel_registerName("setDelegate:");
        g_bridge.objc_msgSend(nsApp, sel_setDel, win_delegate);
    }
}

static intptr_t hook_applicationShouldTerminate(id_t self, sel_t _cmd, id_t sender) {
    (void)self; (void)_cmd; (void)sender;
    /* User chose Quit from Dock menu or Cmd+Q:
     * Inject event kind 8 (Window Close) to Zan event queue, wake event pump,
     * and return 0 (NSTerminateCancel) so the app can run its exit sequence cleanly. */
    if (g_bridge.zan_gui_inject_event) {
        g_bridge.zan_gui_inject_event(0, 8, 0, 0, 0, 0, 0);
    }
    if (g_bridge.zan_gui_wake) {
        g_bridge.zan_gui_wake();
    }
    return 0; /* NSTerminateCancel */
}

static intptr_t hook_windowShouldClose(id_t self, sel_t _cmd, id_t sender) {
    (void)_cmd;
    ensure_app_delegate(self);
    if (g_bridge.zan_gui_inject_event) {
        g_bridge.zan_gui_inject_event((intptr_t)sender, 8, 0, 0, 0, 0, 0);
    }
    if (g_bridge.zan_gui_wake) {
        g_bridge.zan_gui_wake();
    }
    return 0; /* NO */
}

static imp_t g_orig_canBecomeKeyWindow = NULL;

static intptr_t hook_canBecomeKeyWindow(id_t self, sel_t _cmd) {
    /* Called early during window initialization: ensure NSApp has our delegate */
    if (g_bridge.initialized) {
        sel_t sel_del = g_bridge.sel_registerName("delegate");
        if (sel_del) {
            id_t del = g_bridge.objc_msgSend(self, sel_del);
            if (del) ensure_app_delegate(del);
        }
    }
    if (g_orig_canBecomeKeyWindow) {
        return (intptr_t)g_orig_canBecomeKeyWindow(self, _cmd);
    }
    return 1; /* YES */
}

static void zan_mac_gui_fixup(void) {
    if (g_bridge.initialized) return;
    g_bridge.objc_getClass = (class_t (*)(const char *))dlsym(RTLD_DEFAULT, "objc_getClass");
    g_bridge.sel_registerName = (sel_t (*)(const char *))dlsym(RTLD_DEFAULT, "sel_registerName");
    g_bridge.objc_msgSend = (imp_t)dlsym(RTLD_DEFAULT, "objc_msgSend");
    g_bridge.class_addMethod = (int (*)(class_t, sel_t, imp_t, const char *))dlsym(RTLD_DEFAULT, "class_addMethod");
    g_bridge.class_getInstanceMethod = (method_t (*)(class_t, sel_t))dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    g_bridge.method_getImplementation = (imp_t (*)(method_t))dlsym(RTLD_DEFAULT, "method_getImplementation");
    g_bridge.method_setImplementation = (imp_t (*)(method_t, imp_t))dlsym(RTLD_DEFAULT, "method_setImplementation");
    g_bridge.zan_gui_inject_event = (int32_t (*)(intptr_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t))dlsym(RTLD_DEFAULT, "zan_gui_inject_event");
    g_bridge.zan_gui_wake = (int32_t (*)(void))dlsym(RTLD_DEFAULT, "zan_gui_wake");

    if (!g_bridge.objc_getClass || !g_bridge.sel_registerName || !g_bridge.objc_msgSend) {
        return;
    }
    g_bridge.initialized = 1;

    /* 1. Add applicationShouldTerminate: to ZanDelegate */
    class_t cls_del = g_bridge.objc_getClass("ZanDelegate");
    if (cls_del && g_bridge.class_addMethod) {
        sel_t sel_term = g_bridge.sel_registerName("applicationShouldTerminate:");
        g_bridge.class_addMethod(cls_del, sel_term, (imp_t)hook_applicationShouldTerminate, "q@:@");

        /* Swizzle windowShouldClose: to ensure it wakes event pump */
        if (g_bridge.class_getInstanceMethod && g_bridge.method_setImplementation) {
            sel_t sel_close = g_bridge.sel_registerName("windowShouldClose:");
            method_t m_close = g_bridge.class_getInstanceMethod(cls_del, sel_close);
            if (m_close) {
                g_bridge.method_setImplementation(m_close, (imp_t)hook_windowShouldClose);
            }
        }
    }

    /* 2. Swizzle ZanWindow canBecomeKeyWindow to ensure NSApp.delegate is set early */
    class_t cls_win = g_bridge.objc_getClass("ZanWindow");
    if (cls_win && g_bridge.class_getInstanceMethod && g_bridge.method_setImplementation) {
        sel_t sel_canKey = g_bridge.sel_registerName("canBecomeKeyWindow");
        method_t m_key = g_bridge.class_getInstanceMethod(cls_win, sel_canKey);
        if (m_key) {
            g_orig_canBecomeKeyWindow = g_bridge.method_getImplementation(m_key);
            g_bridge.method_setImplementation(m_key, (imp_t)hook_canBecomeKeyWindow);
        }
    }
}

__attribute__((constructor))
static void zan_mac_gui_init(void) {
    zan_mac_gui_fixup();
}

__attribute__((visibility("default")))
int64_t zan_gui_event_seq(void) {
    zan_mac_gui_fixup();
    static int64_t s_seq = 1;
    return s_seq++;
}

__attribute__((visibility("default")))
int32_t zan_gui_event_flag(void) {
    return 0;
}

__attribute__((visibility("default")))
void zan_gui_present_full(void) {
    zan_mac_gui_fixup();
    /* macOS draws full surface on every swap */
}

extern int32_t zan_gui_font_height(int32_t font_size);

__attribute__((visibility("default")))
int32_t zan_gui_font_ascent(int32_t font_size) {
    if (font_size <= 0) return 0;
    int32_t h = zan_gui_font_height(font_size);
    if (h <= 0) h = font_size;
    return (int32_t)((h * 4 + 2) / 5);
}

extern void zan_gui_draw_text(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size);

__attribute__((visibility("default")))
void zan_gui_draw_text_bold(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size) {
    zan_gui_draw_text(surface_id, x, y, text, color, font_size);
}

extern void zan_gui_draw_polyline(int32_t surface_id, const int32_t *pts, int32_t n, int32_t color, int32_t thickness);

__attribute__((visibility("default")))
void zan_gui_draw_polybatch(int32_t surface_id, const int32_t *pts, const int32_t *counts, int32_t n_paths, int32_t color, int32_t thickness) {
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
