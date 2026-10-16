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
#include <stdlib.h>
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

/*
 * Objective-C message dispatch signatures for Apple ARM64 ABI.
 * On ARM64, variadic functions pass extra arguments on the stack, while
 * methods receive them in registers (x2..x7, d0..d7). We must use exact
 * non-variadic function pointers for objc_msgSend to avoid uninitialized
 * register state and immediate launch crashes.
 */
typedef zan_id_t (*zan_msg0_fn)(zan_id_t, zan_sel_t);
typedef zan_id_t (*zan_msg1_fn)(zan_id_t, zan_sel_t, zan_id_t);
typedef void (*zan_msg1_bool_fn)(zan_id_t, zan_sel_t, bool);
typedef void (*zan_msg1_dbl_fn)(zan_id_t, zan_sel_t, double);
typedef zan_id_t (*zan_msg1_str_fn)(zan_id_t, zan_sel_t, const char *);
typedef zan_id_t (*zan_msg2_fn)(zan_id_t, zan_sel_t, zan_id_t, zan_id_t);

typedef struct {
    double x, y;
} zan_cg_point_t;

typedef struct {
    double width, height;
} zan_cg_size_t;

typedef struct {
    zan_cg_point_t origin;
    zan_cg_size_t size;
} zan_cg_rect_t;

typedef zan_cg_rect_t (*zan_msg_rect0_fn)(zan_id_t, zan_sel_t);
typedef double (*zan_msg_dbl0_fn)(zan_id_t, zan_sel_t);
typedef zan_id_t (*zan_msg_init_rect_fn)(zan_id_t, zan_sel_t, zan_cg_rect_t);
typedef zan_cg_point_t (*zan_msg_loc_fn)(zan_id_t, zan_sel_t, zan_id_t);

typedef void *CGColorSpaceRef;
typedef void *CGDataProviderRef;
typedef void *CGImageRef;
typedef void (*CGDataProviderReleaseDataCallback)(void *info, const void *data, size_t size);

static CGColorSpaceRef (*p_CGColorSpaceCreateDeviceRGB)(void) = NULL;
static void (*p_CGColorSpaceRelease)(CGColorSpaceRef space) = NULL;
static CGDataProviderRef (*p_CGDataProviderCreateWithData)(void *info, const void *data, size_t size, CGDataProviderReleaseDataCallback releaseData) = NULL;
static void (*p_CGDataProviderRelease)(CGDataProviderRef provider) = NULL;
static CGImageRef (*p_CGImageCreate)(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel, size_t bytesPerRow,
                                     CGColorSpaceRef space, uint32_t bitmapInfo, CGDataProviderRef provider,
                                     const double *decode, bool shouldInterpolate, int intent) = NULL;
static void (*p_CGImageRelease)(CGImageRef image) = NULL;
static int (*p_UIApplicationMain)(int argc, char **argv, zan_id_t principalClassName, zan_id_t delegateClassName) = NULL;

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

static void (*p_dispatch_sync_f)(void *queue, void *context, void (*work)(void *)) = NULL;
static void *p_dispatch_main_q = NULL;
static int (*p_pthread_main_np)(void) = NULL;

static inline zan_id_t zan_send0(zan_id_t receiver, zan_sel_t sel) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return NULL;
    return ((zan_msg0_fn)g_bridge.objc_msgSend)(receiver, sel);
}

static inline zan_id_t zan_send1(zan_id_t receiver, zan_sel_t sel, zan_id_t arg1) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return NULL;
    return ((zan_msg1_fn)g_bridge.objc_msgSend)(receiver, sel, arg1);
}

static inline void zan_send1_bool(zan_id_t receiver, zan_sel_t sel, bool arg1) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return;
    ((zan_msg1_bool_fn)g_bridge.objc_msgSend)(receiver, sel, arg1);
}

static inline void zan_send1_dbl(zan_id_t receiver, zan_sel_t sel, double arg1) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return;
    ((zan_msg1_dbl_fn)g_bridge.objc_msgSend)(receiver, sel, arg1);
}

static inline zan_id_t zan_send1_str(zan_id_t receiver, zan_sel_t sel, const char *arg1) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return NULL;
    return ((zan_msg1_str_fn)g_bridge.objc_msgSend)(receiver, sel, arg1);
}

static inline zan_id_t zan_send2(zan_id_t receiver, zan_sel_t sel, zan_id_t arg1, zan_id_t arg2) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return NULL;
    return ((zan_msg2_fn)g_bridge.objc_msgSend)(receiver, sel, arg1, arg2);
}

static inline zan_id_t zan_send_init_rect(zan_id_t receiver, zan_sel_t sel, zan_cg_rect_t rect) {
    if (!receiver || !sel || !g_bridge.objc_msgSend) return NULL;
    return ((zan_msg_init_rect_fn)g_bridge.objc_msgSend)(receiver, sel, rect);
}

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

static void zan_ios_load_frameworks(void) {
    dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_GLOBAL | RTLD_NOW);
    dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_GLOBAL | RTLD_NOW);
    dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_GLOBAL | RTLD_NOW);
    dlopen("/System/Library/Frameworks/UIKit.framework/UIKit", RTLD_GLOBAL | RTLD_NOW);
}

static void zan_ios_init_bridge(void) {
    if (g_bridge.initialized) return;

    zan_ios_load_frameworks();

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

    p_dispatch_sync_f = (void (*)(void *, void *, void (*)(void *)))dlsym(RTLD_DEFAULT, "dispatch_sync_f");
    p_dispatch_main_q = dlsym(RTLD_DEFAULT, "_dispatch_main_q");
    p_pthread_main_np = (int (*)(void))dlsym(RTLD_DEFAULT, "pthread_main_np");

    p_CGColorSpaceCreateDeviceRGB = (CGColorSpaceRef (*)(void))dlsym(RTLD_DEFAULT, "CGColorSpaceCreateDeviceRGB");
    p_CGColorSpaceRelease = (void (*)(CGColorSpaceRef))dlsym(RTLD_DEFAULT, "CGColorSpaceRelease");
    p_CGDataProviderCreateWithData = (CGDataProviderRef (*)(void *, const void *, size_t, CGDataProviderReleaseDataCallback))dlsym(RTLD_DEFAULT, "CGDataProviderCreateWithData");
    p_CGDataProviderRelease = (void (*)(CGDataProviderRef))dlsym(RTLD_DEFAULT, "CGDataProviderRelease");
    p_CGImageCreate = (CGImageRef (*)(size_t, size_t, size_t, size_t, size_t, CGColorSpaceRef, uint32_t, CGDataProviderRef, const double *, bool, int))dlsym(RTLD_DEFAULT, "CGImageCreate");
    p_CGImageRelease = (void (*)(CGImageRef))dlsym(RTLD_DEFAULT, "CGImageRelease");
    p_UIApplicationMain = (int (*)(int, char **, zan_id_t, zan_id_t))dlsym(RTLD_DEFAULT, "UIApplicationMain");

    if (g_bridge.objc_getClass && g_bridge.sel_registerName && g_bridge.objc_msgSend) {
        g_bridge.initialized = 1;
    }
}

static void zan_ios_handle_touch(zan_id_t view, zan_id_t touches, int kind) {
    if (!touches) return;
    zan_sel_t sel_any = g_bridge.sel_registerName("anyObject");
    zan_id_t touch = zan_send0(touches, sel_any);
    if (!touch) return;

    zan_sel_t sel_loc = g_bridge.sel_registerName("locationInView:");
    zan_msg_loc_fn fn_loc = (zan_msg_loc_fn)g_bridge.objc_msgSend;
    zan_cg_point_t pt = fn_loc(touch, sel_loc, view);

    zan_sel_t sel_scale = g_bridge.sel_registerName("contentScaleFactor");
    zan_msg_dbl0_fn fn_scale = (zan_msg_dbl0_fn)g_bridge.objc_msgSend;
    double scale = fn_scale(view, sel_scale);
    if (scale < 1.0) scale = 1.0;

    int x = (int)(pt.x * scale);
    int y = (int)(pt.y * scale);

    pthread_mutex_lock(&g_iq_lock);
    iq_push_locked(kind, x, y, 1, 0, 0);
    pthread_mutex_unlock(&g_iq_lock);
}

static void zan_ios_touches_began(zan_id_t self, zan_sel_t _cmd, zan_id_t touches, zan_id_t event) {
    (void)_cmd; (void)event;
    zan_ios_handle_touch(self, touches, 2);
}

static void zan_ios_touches_moved(zan_id_t self, zan_sel_t _cmd, zan_id_t touches, zan_id_t event) {
    (void)_cmd; (void)event;
    zan_ios_handle_touch(self, touches, 1);
}

static void zan_ios_touches_ended(zan_id_t self, zan_sel_t _cmd, zan_id_t touches, zan_id_t event) {
    (void)_cmd; (void)event;
    zan_ios_handle_touch(self, touches, 3);
}

static void zan_ios_touches_cancelled(zan_id_t self, zan_sel_t _cmd, zan_id_t touches, zan_id_t event) {
    (void)_cmd; (void)event;
    zan_ios_handle_touch(self, touches, 3);
}

static bool zan_ios_did_finish_launching(zan_id_t self, zan_sel_t _cmd, zan_id_t app, zan_id_t opts) {
    (void)self; (void)_cmd; (void)app; (void)opts;

    zan_class_t cls_screen = g_bridge.objc_getClass("UIScreen");
    zan_id_t screen = NULL;
    if (cls_screen) {
        zan_sel_t sel_main = g_bridge.sel_registerName("mainScreen");
        screen = zan_send0((zan_id_t)cls_screen, sel_main);
    }

    zan_cg_rect_t bounds = { {0.0, 0.0}, {390.0, 844.0} };
    double scale = 2.0;

    if (screen) {
        zan_sel_t sel_bounds = g_bridge.sel_registerName("bounds");
        zan_msg_rect0_fn fn_bounds = (zan_msg_rect0_fn)g_bridge.objc_msgSend;
        bounds = fn_bounds(screen, sel_bounds);

        zan_sel_t sel_scale = g_bridge.sel_registerName("scale");
        zan_msg_dbl0_fn fn_scale = (zan_msg_dbl0_fn)g_bridge.objc_msgSend;
        scale = fn_scale(screen, sel_scale);
    }

    if (scale < 1.0) scale = 2.0;
    if (bounds.size.width < 100.0 || bounds.size.height < 100.0) {
        bounds.origin.x = 0;
        bounds.origin.y = 0;
        bounds.size.width = 390.0;
        bounds.size.height = 844.0;
    }

    g_iwin.dpi = (int)(96.0 * scale);
    g_iwin.width = (int)(bounds.size.width * scale);
    g_iwin.height = (int)(bounds.size.height * scale);

    zan_class_t cls_win = g_bridge.objc_getClass("UIWindow");
    zan_sel_t sel_alloc = g_bridge.sel_registerName("alloc");
    zan_sel_t sel_init_frame = g_bridge.sel_registerName("initWithFrame:");

    zan_id_t win_alloc = zan_send0((zan_id_t)cls_win, sel_alloc);
    zan_id_t win = zan_send_init_rect(win_alloc, sel_init_frame, bounds);

    zan_class_t cls_vc = g_bridge.objc_getClass("UIViewController");
    zan_sel_t sel_init = g_bridge.sel_registerName("init");
    zan_id_t vc_alloc = zan_send0((zan_id_t)cls_vc, sel_alloc);
    zan_id_t vc = zan_send0(vc_alloc, sel_init);

    zan_class_t cls_zanview = g_bridge.objc_getClass("ZanView");
    if (!cls_zanview) cls_zanview = g_bridge.objc_getClass("UIView");
    zan_id_t view_alloc = zan_send0((zan_id_t)cls_zanview, sel_alloc);
    zan_id_t view = zan_send_init_rect(view_alloc, sel_init_frame, bounds);

    zan_sel_t sel_set_scale = g_bridge.sel_registerName("setContentScaleFactor:");
    zan_send1_dbl(view, sel_set_scale, scale);

    zan_sel_t sel_layer = g_bridge.sel_registerName("layer");
    zan_id_t layer = zan_send0(view, sel_layer);
    if (layer) {
        zan_sel_t sel_set_contents_scale = g_bridge.sel_registerName("setContentsScale:");
        zan_send1_dbl(layer, sel_set_contents_scale, scale);
        zan_sel_t sel_set_opaque = g_bridge.sel_registerName("setOpaque:");
        zan_send1_bool(layer, sel_set_opaque, true);
    }

    zan_sel_t sel_set_multi = g_bridge.sel_registerName("setMultipleTouchEnabled:");
    zan_send1_bool(view, sel_set_multi, true);

    zan_sel_t sel_set_view = g_bridge.sel_registerName("setView:");
    zan_send1(vc, sel_set_view, view);

    zan_sel_t sel_set_root = g_bridge.sel_registerName("setRootViewController:");
    zan_send1(win, sel_set_root, vc);

    zan_sel_t sel_make_key = g_bridge.sel_registerName("makeKeyAndVisible");
    zan_send0(win, sel_make_key);

    g_iwin.ui_window = win;
    g_iwin.ui_controller = vc;
    g_iwin.ui_view = view;
    g_iwin.attached = 1;
    g_iwin.closed = 0;

    pthread_mutex_lock(&g_iq_lock);
    iq_push_locked(0, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_iq_lock);

    return true;
}

static zan_id_t zan_ios_get_window(zan_id_t self, zan_sel_t _cmd) {
    (void)self; (void)_cmd;
    return g_iwin.ui_window;
}

static void zan_ios_set_window(zan_id_t self, zan_sel_t _cmd, zan_id_t win) {
    (void)self; (void)_cmd;
    g_iwin.ui_window = win;
}

static bool zan_ios_open_url(zan_id_t self, zan_sel_t _cmd, zan_id_t app, zan_id_t url, zan_id_t opts) {
    (void)self; (void)_cmd; (void)app; (void)url; (void)opts;
    return true;
}

static void zan_ios_register_classes(void) {
    zan_class_t cls_uiview = g_bridge.objc_getClass("UIView");
    if (cls_uiview && !g_bridge.objc_getClass("ZanView")) {
        zan_class_t cls_zanview = g_bridge.objc_allocateClassPair(cls_uiview, "ZanView", 0);
        if (cls_zanview) {
            g_bridge.class_addMethod(cls_zanview, g_bridge.sel_registerName("touchesBegan:withEvent:"), (zan_imp_t)zan_ios_touches_began, "v@:@@");
            g_bridge.class_addMethod(cls_zanview, g_bridge.sel_registerName("touchesMoved:withEvent:"), (zan_imp_t)zan_ios_touches_moved, "v@:@@");
            g_bridge.class_addMethod(cls_zanview, g_bridge.sel_registerName("touchesEnded:withEvent:"), (zan_imp_t)zan_ios_touches_ended, "v@:@@");
            g_bridge.class_addMethod(cls_zanview, g_bridge.sel_registerName("touchesCancelled:withEvent:"), (zan_imp_t)zan_ios_touches_cancelled, "v@:@@");
            g_bridge.objc_registerClassPair(cls_zanview);
        }
    }

    zan_class_t cls_uiresponder = g_bridge.objc_getClass("UIResponder");
    if (cls_uiresponder && !g_bridge.objc_getClass("ZanAppDelegate")) {
        zan_class_t cls_appdel = g_bridge.objc_allocateClassPair(cls_uiresponder, "ZanAppDelegate", 0);
        if (cls_appdel) {
            g_bridge.class_addMethod(cls_appdel, g_bridge.sel_registerName("application:didFinishLaunchingWithOptions:"), (zan_imp_t)zan_ios_did_finish_launching, "B@:@@");
            g_bridge.class_addMethod(cls_appdel, g_bridge.sel_registerName("window"), (zan_imp_t)zan_ios_get_window, "@@:");
            g_bridge.class_addMethod(cls_appdel, g_bridge.sel_registerName("setWindow:"), (zan_imp_t)zan_ios_set_window, "v@:@");
            g_bridge.class_addMethod(cls_appdel, g_bridge.sel_registerName("application:openURL:options:"), (zan_imp_t)zan_ios_open_url, "B@:@@@");
            g_bridge.objc_registerClassPair(cls_appdel);
        }
    }
}

extern int zan_user_main(int argc, char **argv) __attribute__((weak));

static int g_zan_argc = 0;
static char **g_zan_argv = NULL;

static void *zan_ios_worker_entry(void *arg) {
    (void)arg;
    while (!g_iwin.attached) {
        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    if (zan_user_main) {
        zan_user_main(g_zan_argc, g_zan_argv);
    }
    return NULL;
}

int main(int argc, char **argv) {
    g_zan_argc = argc;
    g_zan_argv = argv;

    if (argv && argv[0]) {
        char exe_dir[1024];
        snprintf(exe_dir, sizeof(exe_dir), "%s", argv[0]);
        char *slash = strrchr(exe_dir, '/');
        if (slash) {
            *slash = '\0';
            chdir(exe_dir);
        }
    }

    zan_ios_init_bridge();
    zan_ios_register_classes();

    pthread_t tid;
    pthread_create(&tid, NULL, zan_ios_worker_entry, NULL);

    void *(*p_objc_autoreleasePoolPush)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPush");
    void (*p_objc_autoreleasePoolPop)(void *) = (void (*)(void *))dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPop");
    void *pool = p_objc_autoreleasePoolPush ? p_objc_autoreleasePoolPush() : NULL;

    zan_class_t cls_str = g_bridge.objc_getClass("NSString");
    zan_sel_t sel_utf8 = g_bridge.sel_registerName("stringWithUTF8String:");
    zan_id_t del_name = zan_send1_str((zan_id_t)cls_str, sel_utf8, "ZanAppDelegate");

    int ret = 0;
    if (p_UIApplicationMain) {
        ret = p_UIApplicationMain(argc, argv, NULL, del_name);
    }

    if (pool && p_objc_autoreleasePoolPop) {
        p_objc_autoreleasePoolPop(pool);
    }
    return ret;
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title; (void)width; (void)height;
    while (!g_iwin.attached) {
        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
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

EXPORT i32 zan_gui_window_width(void)  { return g_iwin.width > 0 ? g_iwin.width : 390; }
EXPORT i32 zan_gui_window_height(void) { return g_iwin.height > 0 ? g_iwin.height : 844; }
EXPORT i32 zan_gui_client_width(iptr hwnd_val)  { (void)hwnd_val; return zan_gui_window_width(); }
EXPORT i32 zan_gui_client_height(iptr hwnd_val) { (void)hwnd_val; return zan_gui_window_height(); }

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

typedef struct {
    zan_id_t layer;
    CGImageRef img;
} zan_present_ctx_t;

static void zan_ios_do_present(void *arg) {
    zan_present_ctx_t *ctx = (zan_present_ctx_t *)arg;
    if (ctx && ctx->layer && ctx->img) {
        zan_class_t cls_tx = g_bridge.objc_getClass("CATransaction");
        if (cls_tx) {
            zan_sel_t sel_begin = g_bridge.sel_registerName("begin");
            zan_sel_t sel_set_disable = g_bridge.sel_registerName("setDisableActions:");
            zan_sel_t sel_commit = g_bridge.sel_registerName("commit");
            zan_send0((zan_id_t)cls_tx, sel_begin);
            zan_send1_bool((zan_id_t)cls_tx, sel_set_disable, true);
            zan_sel_t sel_set_contents = g_bridge.sel_registerName("setContents:");
            zan_send1(ctx->layer, sel_set_contents, (zan_id_t)ctx->img);
            zan_send0((zan_id_t)cls_tx, sel_commit);
        } else {
            zan_sel_t sel_set_contents = g_bridge.sel_registerName("setContents:");
            zan_send1(ctx->layer, sel_set_contents, (zan_id_t)ctx->img);
        }
    }
}

EXPORT void zan_gui_present_full(void) {}

EXPORT i32 zan_gui_present(iptr hwnd_val, i32 surface_id) {
    (void)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) {
        return 1;
    }
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    if (!p_CGColorSpaceCreateDeviceRGB || !p_CGDataProviderCreateWithData || !p_CGImageCreate) {
        return 0;
    }
    if (s->width <= 0 || s->height <= 0 || s->stride < s->width || !s->pixels) {
        return 1;
    }

    CGColorSpaceRef cs = p_CGColorSpaceCreateDeviceRGB();
    if (!cs) return 1;

    size_t bytes_per_row = (size_t)s->stride * sizeof(uint32_t);
    size_t total_bytes = bytes_per_row * (size_t)s->height;

    CGDataProviderRef provider = p_CGDataProviderCreateWithData(
        NULL, s->pixels, total_bytes, NULL);
    if (!provider) {
        p_CGColorSpaceRelease(cs);
        return 1;
    }

    /* 32-bit ARGB little-endian (BGRA: kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst) */
    uint32_t bitmap_info = (4 << 12) | 2;
    CGImageRef img = p_CGImageCreate(
        (size_t)s->width, (size_t)s->height,
        8, 32, bytes_per_row,
        cs, bitmap_info, provider,
        NULL, false, 0
    );

    if (img) {
        zan_sel_t sel_layer = g_bridge.sel_registerName("layer");
        zan_id_t layer = zan_send0(g_iwin.ui_view, sel_layer);
        if (layer) {
            zan_present_ctx_t ctx = { layer, img };
            if (p_pthread_main_np && p_pthread_main_np()) {
                zan_ios_do_present(&ctx);
            } else if (p_dispatch_sync_f && p_dispatch_main_q) {
                p_dispatch_sync_f(p_dispatch_main_q, &ctx, zan_ios_do_present);
            } else {
                zan_ios_do_present(&ctx);
            }
        }
        p_CGImageRelease(img);
    }

    p_CGDataProviderRelease(provider);
    p_CGColorSpaceRelease(cs);
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
