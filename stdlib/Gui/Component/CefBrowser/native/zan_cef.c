/* zan_cef -- native driver behind Gui.Component.CefBrowser.
 *
 * The Chromium Embedded Framework runtime is NOT linked: it is downloaded per
 * machine by CefRuntime.zan into a cache directory and opened here with
 * dlopen/LoadLibrary, so a Zan program carries no 1.4 GB payload and a machine
 * without CEF still runs (every entry point degrades to a no-op and
 * zan_cef_last_error() explains why).
 *
 * Only libcef's exported C functions are resolved dynamically; the struct
 * layouts come from CEF headers at build time. Modern CEF (>= 127) versions
 * its C API, so this translation unit is compiled twice: once with
 * -DCEF_API_VERSION=<n> against current headers (compatible with every CEF
 * whose supported range covers <n>) and once with -DZAN_CEF_LEGACY against
 * CEF 109 headers, the last branch supporting Windows 7/8.1. CefRuntime picks
 * the matching pair at run time.
 *
 * Rich page control (JavaScript results, cookies, screenshots, request
 * interception) is not mirrored one function at a time: the driver exposes the
 * DevTools protocol (CDP) as a message pipe, which is exactly the surface
 * Chromium itself is automated with.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/cef_api_hash.h"
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_command_line_capi.h"
#include "include/capi/cef_devtools_message_observer_capi.h"
#include "include/capi/cef_focus_handler_capi.h"
#include "include/capi/cef_registration_capi.h"

#ifdef _WIN32
#include <windows.h>
#define ZC_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#define ZC_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#endif

/* ---------------------------------------------------------------- utilities */

#define ZC_MAX_BROWSERS 64
#define ZC_MAX_CDP_QUEUE 512

static char zc_error[512];

/* Diagnostics gated on ZAN_CEF_LOG=1 (env read once): CEF failures show up as
 * a blank view or a stalled page, and the useful signal (what switches the
 * child processes got, whether the host kept pumping) is only visible here. */
static int zc_log_on(void) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("ZAN_CEF_LOG");
        on = (v && v[0] && strcmp(v, "0") != 0) ? 1 : 0;
    }
    return on;
}

static void zc_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(zc_error, sizeof(zc_error), fmt, ap);
    va_end(ap);
}

static char *zc_dup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ------------------------------------------------------------ libcef loader */

typedef int (*zc_fn_initialize)(const cef_main_args_t *, const cef_settings_t *,
                                cef_app_t *, void *);
typedef int (*zc_fn_execute_process)(const cef_main_args_t *, cef_app_t *, void *);
typedef void (*zc_fn_void)(void);
typedef const char *(*zc_fn_api_hash)(int, int);
typedef cef_browser_t *(*zc_fn_create_browser_sync)(const cef_window_info_t *,
                                                    cef_client_t *,
                                                    const cef_string_t *,
                                                    const cef_browser_settings_t *,
                                                    cef_dictionary_value_t *,
                                                    cef_request_context_t *);
typedef int (*zc_fn_utf8_to_utf16)(const char *, size_t, cef_string_utf16_t *);
/* The UTF-16 code unit is spelled `char16` before CEF 120 and `char16_t` after,
 * so the source string is taken as void* to keep one source for both branches
 * (only its address is passed through). */
typedef int (*zc_fn_utf16_to_utf8)(const void *, size_t, cef_string_utf8_t *);
typedef void (*zc_fn_utf16_clear)(cef_string_utf16_t *);
typedef void (*zc_fn_utf8_clear)(cef_string_utf8_t *);
typedef void (*zc_fn_userfree_utf16_free)(cef_string_userfree_utf16_t);

typedef struct {
    void *lib;
    zc_fn_api_hash api_hash;
    zc_fn_initialize initialize;
    zc_fn_execute_process execute_process;
    zc_fn_void shutdown;
    zc_fn_void do_message_loop_work;
    zc_fn_void quit_message_loop;
    zc_fn_create_browser_sync create_browser_sync;
    zc_fn_utf8_to_utf16 utf8_to_utf16;
    zc_fn_utf16_to_utf8 utf16_to_utf8;
    zc_fn_utf16_clear utf16_clear;
    zc_fn_utf8_clear utf8_clear;
    zc_fn_userfree_utf16_free userfree_utf16_free;
} zc_cef_t;

static zc_cef_t zc;

static void *zc_open(const char *path) {
#ifdef _WIN32
    /* libcef.dll pulls in siblings (chrome_elf.dll, libEGL.dll, ...) from its
     * own directory, which is not on the loader's search path: without
     * LOAD_WITH_ALTERED_SEARCH_PATH the load fails with ERROR_MOD_NOT_FOUND
     * even though the whole runtime is unpacked next to it. */
    /* That flag also wants a real Win32 path, so '/' becomes '\\'. */
    char win[1024];
    size_t n = strlen(path);
    if (n >= sizeof(win)) { return NULL; }
    for (size_t i = 0; i <= n; i++) {
        win[i] = path[i] == '/' ? '\\' : path[i];
    }
    return (void *)LoadLibraryExA(win, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
}

static void *zc_sym(void *lib, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

static const char *zc_open_error(void) {
#ifdef _WIN32
    static char buf[256];
    snprintf(buf, sizeof(buf), "LoadLibrary failed (%lu)", (unsigned long)GetLastError());
    return buf;
#else
    const char *e = dlerror();
    return e ? e : "dlopen failed";
#endif
}

#ifdef __APPLE__

/* ------------------------------------------------------------ macOS NSView */

/* On macOS the browser is an NSView inside the host window, so moving and
 * hiding it means talking to AppKit. That happens through dlsym'd objc_msgSend
 * so this file stays a plain C dylib -- no ObjC compiler, no AppKit at link
 * time -- the same way the Linux path reaches X11 without linking libX11. */
typedef struct { double x, y, w, h; } zc_rect_t;

static struct {
    void *objc;
    void *(*sel)(const char *);
    void *(*get_class)(const char *);
    void *msg;
    void *msg_stret;
} zc_ns;

static int zc_ns_init(void) {
    if (zc_ns.objc) return 1;
    void *lib = zc_open("/usr/lib/libobjc.A.dylib");
    if (!lib) return 0;
    void *sel = zc_sym(lib, "sel_registerName");
    void *msg = zc_sym(lib, "objc_msgSend");
    if (!sel || !msg) return 0;
    memcpy(&zc_ns.sel, &sel, sizeof(void *));
    zc_ns.msg = msg;
    /* NSRect returns: arm64 hands small aggregates back in registers, x86_64
     * needs the _stret entry point (absent on arm64). */
    zc_ns.msg_stret = zc_sym(lib, "objc_msgSend_stret");
    zc_ns.objc = lib;
    return 1;
}

static void *zc_ns_id(void *obj, const char *name) {
    if (!obj || !zc_ns_init()) return NULL;
    void *(*send)(void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    return send(obj, zc_ns.sel(name));
}

static int zc_ns_bool(void *obj, const char *name) {
    if (!obj || !zc_ns_init()) return 0;
    signed char (*send)(void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    return send(obj, zc_ns.sel(name)) ? 1 : 0;
}

static void zc_ns_set_bool(void *obj, const char *name, int value) {
    if (!obj || !zc_ns_init()) return;
    void (*send)(void *, void *, signed char) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    send(obj, zc_ns.sel(name), (signed char)(value ? 1 : 0));
}

static zc_rect_t zc_ns_rect(void *obj, const char *name) {
    zc_rect_t r = { 0, 0, 0, 0 };
    if (!obj || !zc_ns_init()) return r;
#if defined(__x86_64__)
    if (!zc_ns.msg_stret) return r;
    void (*send)(zc_rect_t *, void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg_stret, sizeof(void *));
    send(&r, obj, zc_ns.sel(name));
#else
    zc_rect_t (*send)(void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    r = send(obj, zc_ns.sel(name));
#endif
    return r;
}

static void zc_ns_set_frame(void *obj, zc_rect_t r) {
    if (!obj || !zc_ns_init()) return;
    void (*send)(void *, void *, zc_rect_t) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    send(obj, zc_ns.sel("setFrame:"), r);
}

static double zc_ns_double(void *obj, const char *name) {
    if (!obj || !zc_ns_init()) return 0.0;
    double (*send)(void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    return send(obj, zc_ns.sel(name));
}

static void *zc_ns_class(const char *name) {
    if (!zc_ns_init()) return NULL;
    if (!zc_ns.get_class) {
        void *f = zc_sym(zc_ns.objc, "objc_getClass");
        memcpy(&zc_ns.get_class, &f, sizeof(void *));
        if (!zc_ns.get_class) return NULL;
    }
    return zc_ns.get_class(name);
}

static void zc_ns_set_id(void *obj, const char *name, void *value) {
    if (!obj || !zc_ns_init()) return;
    void (*send)(void *, void *, void *) = NULL;
    memcpy(&send, &zc_ns.msg, sizeof(void *));
    send(obj, zc_ns.sel(name), value);
}

/* Chromium's mac message pump talks to NSApp through CrAppProtocol: it calls
 * -isHandlingSendEvent to tell "inside AppKit's event dispatch" from "inside a
 * nested run loop of our own", and every CEF sample gets this by subclassing
 * NSApplication with CefAppProtocol. The host here owns NSApplication (zan_gui
 * created it long before any browser exists), so instead of demanding that
 * every Zan app subclass it, add the two accessors -- and a sendEvent: wrapper
 * that maintains the flag -- to NSApplication itself at startup. Without them
 * Chromium's unrecognized-selector exception kills the process the first time
 * it pumps a nested loop (closing a tab does exactly that). */
static int zc_app_sending_event;

static signed char zc_app_is_handling(void *self, void *sel) {
    (void)self; (void)sel;
    return (signed char)(zc_app_sending_event ? 1 : 0);
}

static void zc_app_set_handling(void *self, void *sel, signed char v) {
    (void)self; (void)sel;
    zc_app_sending_event = v ? 1 : 0;
}

static void (*zc_app_orig_send_event)(void *, void *, void *);

static void zc_app_send_event(void *self, void *sel, void *event) {
    /* Nested: AppKit re-enters sendEvent: while tracking, so save and restore
     * rather than clearing on the way out. */
    int prev = zc_app_sending_event;
    zc_app_sending_event = 1;
    if (zc_app_orig_send_event) zc_app_orig_send_event(self, sel, event);
    zc_app_sending_event = prev;
}

static void zc_mac_patch_nsapp(void) {
    static int done;
    if (done || !zc_ns_init()) return;
    done = 1;
    /* zan_gui already brought AppKit in, but this driver must not depend on
     * that: objc_getClass sees NSApplication only once the framework is in. */
    zc_open("/System/Library/Frameworks/AppKit.framework/AppKit");
    void *cls = zc_ns_class("NSApplication");
    if (!cls) return;
    signed char (*add)(void *, void *, void *, const char *) = NULL;
    void *(*get_method)(void *, void *) = NULL;
    void *(*method_imp)(void *) = NULL;
    void *(*set_imp)(void *, void *) = NULL;
    void *f;
    f = zc_sym(zc_ns.objc, "class_addMethod");
    memcpy(&add, &f, sizeof(void *));
    f = zc_sym(zc_ns.objc, "class_getInstanceMethod");
    memcpy(&get_method, &f, sizeof(void *));
    f = zc_sym(zc_ns.objc, "method_getImplementation");
    memcpy(&method_imp, &f, sizeof(void *));
    f = zc_sym(zc_ns.objc, "method_setImplementation");
    memcpy(&set_imp, &f, sizeof(void *));
    if (!add || !get_method || !method_imp || !set_imp) return;
    void *imp;
    imp = (void *)zc_app_is_handling;
    add(cls, zc_ns.sel("isHandlingSendEvent"), imp, "c@:");
    imp = (void *)zc_app_set_handling;
    add(cls, zc_ns.sel("setHandlingSendEvent:"), imp, "v@:c");
    /* Chromium also asks whether NSApp conforms to the protocol before trusting
     * the flag; the protocol object lives in the CEF framework, which is
     * already loaded by the time this runs. */
    void *(*get_proto)(const char *) = NULL;
    signed char (*add_proto)(void *, void *) = NULL;
    f = zc_sym(zc_ns.objc, "objc_getProtocol");
    memcpy(&get_proto, &f, sizeof(void *));
    f = zc_sym(zc_ns.objc, "class_addProtocol");
    memcpy(&add_proto, &f, sizeof(void *));
    if (get_proto && add_proto) {
        void *p = get_proto("CrAppProtocol");
        if (p) add_proto(cls, p);
        p = get_proto("CrAppControlProtocol");
        if (p) add_proto(cls, p);
    }
    void *m = get_method(cls, zc_ns.sel("sendEvent:"));
    if (m) {
        void *orig = method_imp(m);
        memcpy(&zc_app_orig_send_event, &orig, sizeof(void *));
        imp = (void *)zc_app_send_event;
        set_imp(m, imp);
    }
}

/* CoreGraphics path builders, dlsym'd for the same reason as AppKit above. */
static struct {
    void *lib;
    void *(*path_create)(void);
    void (*path_add_rect)(void *, const void *, zc_rect_t);
    void (*path_release)(void *);
} zc_cg;

static int zc_cg_init(void) {
    if (zc_cg.lib) return 1;
    void *lib = zc_open("/System/Library/Frameworks/CoreGraphics.framework/"
                        "CoreGraphics");
    if (!lib) return 0;
    void *c = zc_sym(lib, "CGPathCreateMutable");
    void *a = zc_sym(lib, "CGPathAddRect");
    void *r = zc_sym(lib, "CGPathRelease");
    if (!c || !a || !r) return 0;
    /* The mask is a CAShapeLayer, and objc_getClass only sees it once
     * QuartzCore is in the process -- AppKit pulls it in, but this driver must
     * not depend on who loaded what. */
    if (!zc_open("/System/Library/Frameworks/QuartzCore.framework/QuartzCore"))
        return 0;
    memcpy(&zc_cg.path_create, &c, sizeof(void *));
    memcpy(&zc_cg.path_add_rect, &a, sizeof(void *));
    memcpy(&zc_cg.path_release, &r, sizeof(void *));
    zc_cg.lib = lib;
    return 1;
}

/* Rects cross this ABI in physical pixels (that is what the host's layout and
 * its software surface use), while AppKit frames are in points, so every rect
 * handed to a view has to be divided by the window's backing scale -- on a
 * Retina display an unscaled rect is twice too large and buries the host UI. */
static double zc_view_scale(void *view) {
    double s = zc_ns_double(zc_ns_id(view, "window"), "backingScaleFactor");
    if (s <= 0.0) return 1.0;
    return s;
}

#endif /* __APPLE__ */

/* Resolve every libcef entry point the driver needs. `runtime_dir` is a CEF
 * runtime as unpacked by CefRuntime.zan; the shared library sits in Release/
 * (Windows, Linux) or Chromium Embedded Framework.framework/ (macOS). */
static int zc_load(const char *runtime_dir) {
    if (zc.lib) return 1;
    if (!runtime_dir || !runtime_dir[0]) {
        zc_fail("cef runtime directory is empty");
        return 0;
    }
    char path[1024];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\Release\\libcef.dll", runtime_dir);
#elif defined(__APPLE__)
    snprintf(path, sizeof(path),
             "%s/Release/Chromium Embedded Framework.framework/"
             "Chromium Embedded Framework", runtime_dir);
#else
    snprintf(path, sizeof(path), "%s/Release/libcef.so", runtime_dir);
#endif
    void *lib = zc_open(path);
    if (!lib) {
        zc_fail("cannot load %s: %s", path, zc_open_error());
        return 0;
    }
#define ZC_BIND(field, name)                                              \
    do {                                                                  \
        void *s = zc_sym(lib, name);                                       \
        if (!s) { zc_fail("libcef export missing: %s", name); return 0; }  \
        memcpy(&zc.field, &s, sizeof(void *));                             \
    } while (0)
    ZC_BIND(initialize, "cef_initialize");
    ZC_BIND(execute_process, "cef_execute_process");
    ZC_BIND(shutdown, "cef_shutdown");
    ZC_BIND(do_message_loop_work, "cef_do_message_loop_work");
    ZC_BIND(quit_message_loop, "cef_quit_message_loop");
    ZC_BIND(create_browser_sync, "cef_browser_host_create_browser_sync");
    ZC_BIND(utf8_to_utf16, "cef_string_utf8_to_utf16");
    ZC_BIND(utf16_to_utf8, "cef_string_utf16_to_utf8");
    ZC_BIND(utf16_clear, "cef_string_utf16_clear");
    ZC_BIND(utf8_clear, "cef_string_utf8_clear");
    ZC_BIND(userfree_utf16_free, "cef_string_userfree_utf16_free");
#undef ZC_BIND
#ifndef ZAN_CEF_LEGACY
    /* Versioned C API: the first cef_api_hash() call fixes the ABI libcef
     * exposes to this client, and it must agree with the headers we compiled
     * against or every struct layout is a guess. */
    void *h = zc_sym(lib, "cef_api_hash");
    if (!h) {
        zc_fail("libcef export missing: cef_api_hash (runtime older than "
                "CEF 127? use the legacy driver)");
        return 0;
    }
    memcpy(&zc.api_hash, &h, sizeof(void *));
    const char *runtime_hash = zc.api_hash(CEF_API_VERSION, 0);
    if (!runtime_hash || strcmp(runtime_hash, CEF_API_HASH_PLATFORM) != 0) {
        zc_fail("cef api hash mismatch: runtime %s, driver %s (api version %d)",
                runtime_hash ? runtime_hash : "?", CEF_API_HASH_PLATFORM,
                CEF_API_VERSION);
        return 0;
    }
#else
    /* CEF 109 predates the versioned API: cef_api_hash takes just the entry
     * index (0 = platform hash), and it is still the only reliable check that
     * the installed runtime matches the headers this variant was built with. */
    void *h = zc_sym(lib, "cef_api_hash");
    if (!h) {
        zc_fail("libcef export missing: cef_api_hash");
        return 0;
    }
    {
        typedef const char *(*zc_fn_api_hash_legacy)(int);
        zc_fn_api_hash_legacy hash_fn;
        memcpy(&hash_fn, &h, sizeof(void *));
        const char *runtime_hash = hash_fn(0);
        if (!runtime_hash || strcmp(runtime_hash, CEF_API_HASH_PLATFORM) != 0) {
            zc_fail("cef api hash mismatch: runtime %s, driver %s",
                    runtime_hash ? runtime_hash : "?", CEF_API_HASH_PLATFORM);
            return 0;
        }
    }
#endif
    zc.lib = lib;
    return 1;
}

/* ------------------------------------------------------------- cef_string_t */

/* Fill a stack cef_string_t with a copy of `s`; release with zc_str_free. */
static void zc_str_set(cef_string_t *out, const char *s) {
    memset(out, 0, sizeof(*out));
    if (s && s[0]) zc.utf8_to_utf16(s, strlen(s), out);
}

static void zc_str_free(cef_string_t *s) { zc.utf16_clear(s); }

/* UTF-8 copy of a CEF string into `buf`. */
static void zc_str_get(const cef_string_t *s, char *buf, size_t cap) {
    buf[0] = '\0';
    if (!s || !s->str || s->length == 0) return;
    cef_string_utf8_t u8;
    memset(&u8, 0, sizeof(u8));
    if (zc.utf16_to_utf8(s->str, s->length, &u8) && u8.str) {
        size_t n = u8.length < cap - 1 ? u8.length : cap - 1;
        memcpy(buf, u8.str, n);
        buf[n] = '\0';
    }
    zc.utf8_clear(&u8);
}

static void zc_userfree_get(cef_string_userfree_t s, char *buf, size_t cap) {
    zc_str_get(s, buf, cap);
    if (s) zc.userfree_utf16_free(s);
}

/* ------------------------------------------------------------- lock (queue) */

#ifdef _WIN32
static CRITICAL_SECTION zc_lock;
static int zc_lock_ready;
static void zc_lock_init(void) {
    if (!zc_lock_ready) { InitializeCriticalSection(&zc_lock); zc_lock_ready = 1; }
}
static void zc_enter(void) { zc_lock_init(); EnterCriticalSection(&zc_lock); }
static void zc_leave(void) { LeaveCriticalSection(&zc_lock); }
#else
static pthread_mutex_t zc_lock = PTHREAD_MUTEX_INITIALIZER;
static void zc_enter(void) { pthread_mutex_lock(&zc_lock); }
static void zc_leave(void) { pthread_mutex_unlock(&zc_lock); }
#endif

/* --------------------------------------------------------- browser registry */

typedef struct zc_browser_s {
    int used;
    int id;                     /* 1-based handle handed to Zan */
    cef_browser_t *browser;     /* owned reference, NULL until created */
    cef_registration_t *cdp_reg;

    /* Handler vtables live inside the record so a callback's `self` pointer
     * identifies the browser without a lookup table. */
    cef_client_t client;
    cef_life_span_handler_t life;
    cef_load_handler_t load;
    cef_display_handler_t display;
    cef_focus_handler_t focus;
    cef_dev_tools_message_observer_t observer;

    int bx, by, bw, bh;         /* last requested bounds, host coordinates */
    char *clip_spec;            /* region the clip/mask was last built for */
    int shown;                  /* last visibility applied; -1 = not yet */
    char url[2048];
    char title[512];
    int loading, can_back, can_forward;
    int nav_seq, last_status, last_error;
    int closing, gone;
    int cdp_id;                 /* last allocated CDP message id */
    int cdp_attached;
    /* Set while the host UI owns the keyboard (an on-canvas text field is
     * focused): Chromium otherwise grabs the Win32 focus back on its own --
     * after a navigation commits, a page calls focus(), a plugin starts -- and
     * the host's address bar silently stops receiving WM_CHAR. */
    int host_focus;
    /* window.open / target=_blank policy: 0 = let CEF open its own popup
     * window, 1 = block, 2 = cancel and hand the URL to the host (a tabbed
     * host opens its own tab). */
    int popup_policy;
    char popup_url[2048];       /* pending handed-to-host popup target */
    int popup_dropped;          /* hosts that never drain must not queue up */
    char *taken_popup;          /* last popup URL handed to Zan, owned here */

    char *queue[ZC_MAX_CDP_QUEUE];
    int q_head, q_count, q_dropped;
    char *taken;                /* last string handed to Zan, owned here */
} zc_browser_t;

static zc_browser_t zc_browsers[ZC_MAX_BROWSERS];
static int zc_ready;            /* cef_initialize() succeeded */
static int zc_shut;             /* cef_shutdown() already called */
static char zc_runtime[1024];

static zc_browser_t *zc_get(int h) {
    if (h < 1 || h > ZC_MAX_BROWSERS) return NULL;
    zc_browser_t *b = &zc_browsers[h - 1];
    if (!b->used || b->gone) return NULL;
    return b;
}

#define ZC_OWNER(self, member) \
    ((zc_browser_t *)((char *)(self) - offsetof(zc_browser_t, member)))

/* --------------------------------------------------------- base_ref_counted */

/* Every handler here is embedded in a zc_browser_t that outlives CEF's
 * references to it (the slot is only reused after on_before_close), so the
 * ref-count operations are inert rather than freeing anything. */
static void CEF_CALLBACK zc_add_ref(cef_base_ref_counted_t *self) { (void)self; }
static int CEF_CALLBACK zc_release(cef_base_ref_counted_t *self) { (void)self; return 0; }
static int CEF_CALLBACK zc_has_one_ref(cef_base_ref_counted_t *self) { (void)self; return 0; }
static int CEF_CALLBACK zc_has_at_least_one_ref(cef_base_ref_counted_t *self) {
    (void)self;
    return 1;
}

static void zc_base_init(cef_base_ref_counted_t *base, size_t size) {
    base->size = size;
    base->add_ref = zc_add_ref;
    base->release = zc_release;
    base->has_one_ref = zc_has_one_ref;
    base->has_at_least_one_ref = zc_has_at_least_one_ref;
}

/* -------------------------------------------------------------- CDP pipeline */

static void zc_queue_push(zc_browser_t *b, const char *msg, size_t len) {
    char *copy = zc_dup(msg, len);
    if (!copy) return;
    zc_enter();
    if (b->q_count == ZC_MAX_CDP_QUEUE) {
        /* Drop the oldest: a Zan side that stopped draining must not be able
         * to grow the queue without bound. */
        free(b->queue[b->q_head]);
        b->queue[b->q_head] = NULL;
        b->q_head = (b->q_head + 1) % ZC_MAX_CDP_QUEUE;
        b->q_count--;
        b->q_dropped++;
    }
    b->queue[(b->q_head + b->q_count) % ZC_MAX_CDP_QUEUE] = copy;
    b->q_count++;
    zc_leave();
}

static int CEF_CALLBACK zc_on_dev_tools_message(
        cef_dev_tools_message_observer_t *self, cef_browser_t *browser,
        const void *message, size_t message_size) {
    (void)browser;
    zc_browser_t *b = ZC_OWNER(self, observer);
    zc_queue_push(b, (const char *)message, message_size);
    /* Handled: the parsed on_dev_tools_method_result / on_dev_tools_event
     * callbacks would deliver the same payload a second time. */
    return 1;
}

static void CEF_CALLBACK zc_on_dev_tools_method_result(
        cef_dev_tools_message_observer_t *self, cef_browser_t *browser,
        int message_id, int success, const void *result, size_t result_size) {
    (void)self; (void)browser; (void)message_id; (void)success;
    (void)result; (void)result_size;
}

static void CEF_CALLBACK zc_on_dev_tools_event(
        cef_dev_tools_message_observer_t *self, cef_browser_t *browser,
        const cef_string_t *method, const void *params, size_t params_size) {
    (void)self; (void)browser; (void)method; (void)params; (void)params_size;
}

static void CEF_CALLBACK zc_on_agent_attached(
        cef_dev_tools_message_observer_t *self, cef_browser_t *browser) {
    (void)browser;
    ZC_OWNER(self, observer)->cdp_attached = 1;
}

static void CEF_CALLBACK zc_on_agent_detached(
        cef_dev_tools_message_observer_t *self, cef_browser_t *browser) {
    (void)browser;
    ZC_OWNER(self, observer)->cdp_attached = 0;
}

/* ------------------------------------------------------------- life span --- */

static void CEF_CALLBACK zc_on_after_created(cef_life_span_handler_t *self,
                                             cef_browser_t *browser) {
    zc_browser_t *b = ZC_OWNER(self, life);
    if (!b->browser) {
        browser->base.add_ref(&browser->base);
        b->browser = browser;
    }
}

/* window.open / target=_blank / ctrl-click. Returning 1 cancels the popup;
 * policy 2 additionally records the target so the host can open a tab. */
static int CEF_CALLBACK zc_on_before_popup(cef_life_span_handler_t *self,
                                           cef_browser_t *browser,
                                           cef_frame_t *frame,
#ifndef ZAN_CEF_LEGACY
                                           int popup_id,
#endif
                                           const cef_string_t *target_url,
                                           const cef_string_t *target_frame_name,
                                           cef_window_open_disposition_t disp,
                                           int user_gesture,
                                           const cef_popup_features_t *features,
                                           cef_window_info_t *window_info,
                                           cef_client_t **client,
                                           cef_browser_settings_t *settings,
                                           cef_dictionary_value_t **extra_info,
                                           int *no_javascript_access) {
    (void)browser; (void)frame; (void)target_frame_name; (void)disp;
    (void)user_gesture; (void)features; (void)window_info; (void)client;
    (void)settings; (void)extra_info; (void)no_javascript_access;
#ifndef ZAN_CEF_LEGACY
    (void)popup_id;
#endif
    zc_browser_t *b = ZC_OWNER(self, life);
    if (b->popup_policy == 0) return 0;
    if (b->popup_policy == 2) {
        zc_enter();
        if (b->popup_url[0]) {
            b->popup_dropped++;
        } else {
            zc_str_get(target_url, b->popup_url, sizeof(b->popup_url));
        }
        zc_leave();
    }
    return 1;
}

static int CEF_CALLBACK zc_do_close(cef_life_span_handler_t *self,
                                    cef_browser_t *browser) {
    (void)browser;
    ZC_OWNER(self, life)->closing = 1;
    return 0; /* let CEF destroy the native window */
}

static void CEF_CALLBACK zc_on_before_close(cef_life_span_handler_t *self,
                                            cef_browser_t *browser) {
    (void)browser;
    zc_browser_t *b = ZC_OWNER(self, life);
    b->gone = 1;
    if (b->cdp_reg) {
        b->cdp_reg->base.release(&b->cdp_reg->base);
        b->cdp_reg = NULL;
    }
    if (b->browser) {
        b->browser->base.release(&b->browser->base);
        b->browser = NULL;
    }
}

/* --------------------------------------------------------------- load state */

static void CEF_CALLBACK zc_on_loading_state_change(cef_load_handler_t *self,
                                                    cef_browser_t *browser,
                                                    int isLoading, int canGoBack,
                                                    int canGoForward) {
    (void)browser;
    zc_browser_t *b = ZC_OWNER(self, load);
    b->loading = isLoading;
    b->can_back = canGoBack;
    b->can_forward = canGoForward;
}

static void CEF_CALLBACK zc_on_load_start(cef_load_handler_t *self,
                                          cef_browser_t *browser,
                                          cef_frame_t *frame,
                                          cef_transition_type_t transition) {
    (void)browser; (void)transition;
    if (!frame->is_main(frame)) return;
    zc_browser_t *b = ZC_OWNER(self, load);
    b->last_error = 0;
    b->nav_seq++;
}

static void CEF_CALLBACK zc_on_load_end(cef_load_handler_t *self,
                                        cef_browser_t *browser,
                                        cef_frame_t *frame, int httpStatusCode) {
    (void)browser;
    if (!frame->is_main(frame)) return;
    zc_browser_t *b = ZC_OWNER(self, load);
    b->last_status = httpStatusCode;
    b->nav_seq++;
}

static void CEF_CALLBACK zc_on_load_error(cef_load_handler_t *self,
                                          cef_browser_t *browser,
                                          cef_frame_t *frame,
                                          cef_errorcode_t errorCode,
                                          const cef_string_t *errorText,
                                          const cef_string_t *failedUrl) {
    (void)browser; (void)errorText; (void)failedUrl;
    if (!frame->is_main(frame)) return;
    zc_browser_t *b = ZC_OWNER(self, load);
    b->last_error = (int)errorCode;
    b->nav_seq++;
}

/* ------------------------------------------------------------ display state */

static void CEF_CALLBACK zc_on_address_change(cef_display_handler_t *self,
                                              cef_browser_t *browser,
                                              cef_frame_t *frame,
                                              const cef_string_t *url) {
    (void)browser;
    if (!frame->is_main(frame)) return;
    zc_browser_t *b = ZC_OWNER(self, display);
    zc_str_get(url, b->url, sizeof(b->url));
    b->nav_seq++;
}

static void CEF_CALLBACK zc_on_title_change(cef_display_handler_t *self,
                                            cef_browser_t *browser,
                                            const cef_string_t *title) {
    (void)browser;
    zc_browser_t *b = ZC_OWNER(self, display);
    zc_str_get(title, b->title, sizeof(b->title));
    b->nav_seq++;
}

/* -------------------------------------------------------------------- focus */

/* Chromium asking for the focus. Denied while the host owns the keyboard, so a
 * page load (or the page itself) cannot steal it from an on-canvas text field.
 * `source` is FOCUS_SOURCE_NAVIGATION or FOCUS_SOURCE_SYSTEM. */
static int CEF_CALLBACK zc_on_set_focus(cef_focus_handler_t *self,
                                        cef_browser_t *browser,
                                        cef_focus_source_t source) {
    (void)browser;
    (void)source;
    return ZC_OWNER(self, focus)->host_focus ? 1 : 0;
}

/* Chromium giving the focus up (tabbing out of the last element): the host gets
 * it, matching what CefBrowser.SyncFocus does on the Zan side. */
static void CEF_CALLBACK zc_on_take_focus(cef_focus_handler_t *self,
                                          cef_browser_t *browser, int next) {
    (void)browser;
    (void)next;
    ZC_OWNER(self, focus)->host_focus = 1;
}

/* ------------------------------------------------------------------- client */

static cef_life_span_handler_t *CEF_CALLBACK zc_get_life_span_handler(
        cef_client_t *self) {
    return &ZC_OWNER(self, client)->life;
}

static cef_load_handler_t *CEF_CALLBACK zc_get_load_handler(cef_client_t *self) {
    return &ZC_OWNER(self, client)->load;
}

static cef_display_handler_t *CEF_CALLBACK zc_get_display_handler(
        cef_client_t *self) {
    return &ZC_OWNER(self, client)->display;
}

static cef_focus_handler_t *CEF_CALLBACK zc_get_focus_handler(
        cef_client_t *self) {
    return &ZC_OWNER(self, client)->focus;
}

static void zc_browser_init_handlers(zc_browser_t *b) {
    memset(&b->client, 0, sizeof(b->client));
    memset(&b->life, 0, sizeof(b->life));
    memset(&b->load, 0, sizeof(b->load));
    memset(&b->display, 0, sizeof(b->display));
    memset(&b->focus, 0, sizeof(b->focus));
    memset(&b->observer, 0, sizeof(b->observer));

    zc_base_init(&b->client.base, sizeof(b->client));
    b->client.get_life_span_handler = zc_get_life_span_handler;
    b->client.get_load_handler = zc_get_load_handler;
    b->client.get_display_handler = zc_get_display_handler;
    b->client.get_focus_handler = zc_get_focus_handler;

    zc_base_init(&b->life.base, sizeof(b->life));
    b->life.on_after_created = zc_on_after_created;
    b->life.on_before_popup = zc_on_before_popup;
    b->life.do_close = zc_do_close;
    b->life.on_before_close = zc_on_before_close;

    zc_base_init(&b->load.base, sizeof(b->load));
    b->load.on_loading_state_change = zc_on_loading_state_change;
    b->load.on_load_start = zc_on_load_start;
    b->load.on_load_end = zc_on_load_end;
    b->load.on_load_error = zc_on_load_error;

    zc_base_init(&b->display.base, sizeof(b->display));
    b->display.on_address_change = zc_on_address_change;
    b->display.on_title_change = zc_on_title_change;

    zc_base_init(&b->focus.base, sizeof(b->focus));
    b->focus.on_set_focus = zc_on_set_focus;
    b->focus.on_take_focus = zc_on_take_focus;

    zc_base_init(&b->observer.base, sizeof(b->observer));
    b->observer.on_dev_tools_message = zc_on_dev_tools_message;
    b->observer.on_dev_tools_method_result = zc_on_dev_tools_method_result;
    b->observer.on_dev_tools_event = zc_on_dev_tools_event;
    b->observer.on_dev_tools_agent_attached = zc_on_agent_attached;
    b->observer.on_dev_tools_agent_detached = zc_on_agent_detached;
}

/* ---------------------------------------------------------------------- app */

/* Switches appended to every Chromium process. Media playback is a first-class
 * reason to embed CEF instead of a system WebView, so autoplay is allowed
 * without a user gesture; the rest keeps a headless-ish embed usable. */
static char zc_extra_switches[1024];

/* Comma-separated switches: appending one of these with a plain value would
 * drop the entries Chromium and CEF already put there (CEF 151 disables
 * GlicActorUi/LensOverlay/... this way), so an app switch has to be merged
 * into the existing value instead of replacing it. */
static int zc_switch_is_list(const char *k) {
    return strcmp(k, "disable-features") == 0 ||
           strcmp(k, "enable-features") == 0 ||
           strcmp(k, "disable-blink-features") == 0 ||
           strcmp(k, "enable-blink-features") == 0;
}

static void zc_append_switch(cef_command_line_t *cl, const char *key,
                             const char *value) {
    cef_string_t k, v;
    zc_str_set(&k, key);
    if (!value) {
        cl->append_switch(cl, &k);
        zc_str_free(&k);
        return;
    }
    char merged[1024];
    const char *out = value;
    if (zc_switch_is_list(key) && cl->has_switch(cl, &k)) {
        char old[768];
        zc_userfree_get(cl->get_switch_value(cl, &k), old, sizeof(old));
        if (old[0]) {
            snprintf(merged, sizeof(merged), "%s,%s", old, value);
            out = merged;
        }
    }
    zc_str_set(&v, out);
    cl->append_switch_with_value(cl, &k, &v);
    zc_str_free(&k);
    zc_str_free(&v);
}

static void CEF_CALLBACK zc_on_before_command_line_processing(
        cef_app_t *self, const cef_string_t *process_type,
        cef_command_line_t *command_line) {
    (void)self; (void)process_type;
    if (!command_line) return;
    zc_append_switch(command_line, "autoplay-policy",
                     "no-user-gesture-required");

    /* "a=b,c" style list handed down from Zan (CefRuntime/CefBrowser). A comma
     * is also the separator inside switch values (disable-features=A,B), so a
     * semicolon anywhere switches the whole list to semicolons and lets those
     * values through intact. */
    char sep = strchr(zc_extra_switches, ';') ? ';' : ',';
    const char *p = zc_extra_switches;
    while (*p) {
        const char *end = strchr(p, sep);
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && len < 512) {
            char item[512];
            memcpy(item, p, len);
            item[len] = '\0';
            char *eq = strchr(item, '=');
            if (eq) {
                *eq = '\0';
                zc_append_switch(command_line, item, eq + 1);
            } else {
                zc_append_switch(command_line, item, NULL);
            }
        }
        if (!end) break;
        p = end + 1;
    }

    /* With ZAN_CEF_LOG=1, report the command line every Chromium process is
     * actually started with -- the only way to tell a switch that never
     * arrived (typo in ZAN_CEF_SWITCHES) from one Chromium ignored. */
    if (zc_log_on()) {
        char utf8[4096];
        zc_userfree_get(command_line->get_command_line_string(command_line),
                        utf8, sizeof(utf8));
        char ptype[128];
        zc_str_get(process_type, ptype, sizeof(ptype));
        fprintf(stderr, "[cmdline] type=%s extra=\"%s\" line=%s\n",
                ptype[0] ? ptype : "browser", zc_extra_switches, utf8);
        fflush(stderr);
    }
}

static cef_app_t zc_app;

static void zc_app_init(void) {
    memset(&zc_app, 0, sizeof(zc_app));
    zc_base_init(&zc_app.base, sizeof(zc_app));
    zc_app.on_before_command_line_processing = zc_on_before_command_line_processing;
}

/* ---------------------------------------------------------------- main args */

/* CEF wants the process command line. Windows reads it from the OS, elsewhere
 * it must be handed argc/argv, which a shared library recovers from the
 * kernel (/proc/self/cmdline on Linux, the crt externs on macOS).
 *
 * Getting the real argv is not cosmetic: Chromium tells a child process what it
 * is from --type= on its own command line, so a helper handed a made-up argv
 * behaves like a second browser process, exits, and the browser kills itself
 * after enough dead GPU children ("GPU process isn't usable. Goodbye."). */
#ifndef _WIN32
static char *zc_argv_storage;
static char *zc_argv_own[64];
static char **zc_argv;
static int zc_argc;

static void zc_argv_fallback(void) {
    static char fallback[] = "zan";
    zc_argv_own[0] = fallback;
    zc_argv_own[1] = NULL;
    zc_argv = zc_argv_own;
    zc_argc = 1;
}

static void zc_read_cmdline(void) {
    if (zc_argc) return;
#ifdef __APPLE__
    /* No /proc on Darwin; the kernel's argv is reachable from a dylib through
     * the crt externs (what Chromium's own mac helper uses). */
    int *ac = _NSGetArgc();
    char ***av = _NSGetArgv();
    if (ac && av && *ac > 0 && *av) {
        zc_argc = *ac;
        zc_argv = *av;
        return;
    }
    zc_argv_fallback();
#else
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) {
        zc_argv_fallback();
        return;
    }
    size_t cap = 65536, len = 0;
    zc_argv_storage = (char *)malloc(cap);
    if (zc_argv_storage) len = fread(zc_argv_storage, 1, cap - 1, f);
    fclose(f);
    if (!zc_argv_storage || len == 0) {
        zc_argv_fallback();
        return;
    }
    zc_argv_storage[len] = '\0';
    zc_argv = zc_argv_own;
    size_t i = 0;
    while (i < len && zc_argc < 63) {
        zc_argv_own[zc_argc++] = zc_argv_storage + i;
        i += strlen(zc_argv_storage + i) + 1;
    }
    zc_argv_own[zc_argc] = NULL;
#endif
}
#endif

static int zc_file_exists(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path, R_OK) == 0;
#endif
}

static void zc_main_args(cef_main_args_t *args) {
    memset(args, 0, sizeof(*args));
#ifdef _WIN32
    args->instance = GetModuleHandle(NULL);
#else
    zc_read_cmdline();
    args->argc = zc_argc;
    args->argv = zc_argv;
#endif
}

/* -------------------------------------------------------------- public: init */

ZC_EXPORT const char *zan_cef_last_error(void) { return zc_error; }

ZC_EXPORT int zan_cef_ready(void) { return zc_ready && !zc_shut; }

/* Run a Chromium helper process (render/gpu/utility) and return its exit code;
 * returns -1 in the browser process, where the caller must continue into
 * zan_cef_init. A Zan program that reuses its own executable as the helper
 * calls this as the very first thing in Main.
 *
 * `switches` is the same list the browser process passes to zan_cef_init:
 * Chromium only forwards the switches it knows about to its children, so a
 * host switch that must hold in every process (logging, GPU selection) has to
 * be re-applied here as well. */
ZC_EXPORT int zan_cef_execute_process(const char *runtime_dir,
                                     const char *switches) {
    if (!zc_load(runtime_dir)) return -1;
    snprintf(zc_extra_switches, sizeof(zc_extra_switches), "%s",
             switches ? switches : "");
    zc_app_init();
    cef_main_args_t args;
    zc_main_args(&args);
    return zc.execute_process(&args, &zc_app, NULL);
}

/* Tell the subprocess executable where the CEF runtime and this driver are.
 *
 * The standalone helper (native/zan_cef_helper.c, what macOS bundles put in
 * Contents/Frameworks/<x> Helper.app) carries no configuration of its own: it
 * dlopens this driver and calls zan_cef_execute_process, and both paths come
 * from here, because Chromium hands its own environment to the children it
 * spawns. A host that stays its own helper never reads these.
 *
 * Not on Windows: there the helper is always the host executable, which
 * resolves everything through CefOptions before Main gets to RunHelper. */
static void zc_export_helper_env(const char *runtime_dir,
                                 const char *switches) {
#ifndef _WIN32
    if (runtime_dir && runtime_dir[0])
        setenv("ZAN_CEF_HELPER_RUNTIME", runtime_dir, 1);
    setenv("ZAN_CEF_HELPER_SWITCHES", switches ? switches : "", 1);
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)(uintptr_t)&zc_export_helper_env, &info)
        && info.dli_fname && info.dli_fname[0])
        setenv("ZAN_CEF_HELPER_DRIVER", info.dli_fname, 1);
#else
    (void)runtime_dir; (void)switches;
#endif
}

/* Bring up the CEF browser process. `runtime_dir` is CefRuntime.EnsureAsync()'s
 * result, `cache_path` a writable profile directory (empty = incognito),
 * `helper_path` the subprocess executable (empty = re-exec this program, which
 * then has to call zan_cef_execute_process first). */
ZC_EXPORT int zan_cef_init(const char *runtime_dir, const char *cache_path,
                           const char *helper_path, const char *locale,
                           const char *switches, int windowless) {
    if (zc_ready) return 1;
    if (zc_shut) {
        zc_fail("cef already shut down in this process");
        return 0;
    }
    if (!zc_load(runtime_dir)) return 0;
    snprintf(zc_runtime, sizeof(zc_runtime), "%s", runtime_dir);
    snprintf(zc_extra_switches, sizeof(zc_extra_switches), "%s",
             switches ? switches : "");
    zc_app_init();
    zc_export_helper_env(runtime_dir, switches);

    cef_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.size = sizeof(settings);
    settings.no_sandbox = 1;
    settings.multi_threaded_message_loop = 0;
    settings.external_message_pump = 0;
    settings.windowless_rendering_enabled = windowless ? 1 : 0;
    settings.log_severity = LOGSEVERITY_WARNING;

    /* The profile directory is the caller's decision (CefHost.ProfileDir picks
     * and locks a per-instance slot); this only passes it on. */
    char profile[1100];
    snprintf(profile, sizeof(profile), "%s", cache_path ? cache_path : "");

    char buf[1200];
    /* Chromium reports a failed CHECK by writing "[FATAL:...]" to its log and
     * then breaking into the debugger (crash code 0x80000003 with a pure libcef
     * backtrace), so the only way to see the reason is to route that log to a
     * file. ZAN_CEF_LOG=1 turns it on; ZAN_CEF_LOG_FILE overrides the path. */
    if (zc_log_on()) {
        const char *lf = getenv("ZAN_CEF_LOG_FILE");
        if (lf && lf[0]) {
            snprintf(buf, sizeof(buf), "%s", lf);
        } else if (profile[0]) {
            snprintf(buf, sizeof(buf), "%s/cef_debug.log", profile);
        } else {
            snprintf(buf, sizeof(buf), "%s/cef_debug.log", runtime_dir);
        }
        zc_str_set(&settings.log_file, buf);
        settings.log_severity = LOGSEVERITY_VERBOSE;
        fprintf(stderr, "[zan_cef] cef log -> %s\n", buf);
    }
    if (helper_path && helper_path[0])
        zc_str_set(&settings.browser_subprocess_path, helper_path);
    if (profile[0]) {
        zc_str_set(&settings.root_cache_path, profile);
        zc_str_set(&settings.cache_path, profile);
    }
    if (locale && locale[0]) zc_str_set(&settings.locale, locale);
#ifndef __APPLE__
    /* Chromium loads icudtl.dat from the libcef directory, ignoring
     * resources_dir_path, so CefRuntime stages Resources/ into Release/ and the
     * whole payload is read from there. Plain Resources/ stays a fallback. */
    snprintf(buf, sizeof(buf), "%s/Release/icudtl.dat", runtime_dir);
    const char *res = zc_file_exists(buf) ? "Release" : "Resources";
    snprintf(buf, sizeof(buf), "%s/%s", runtime_dir, res);
    zc_str_set(&settings.resources_dir_path, buf);
    snprintf(buf, sizeof(buf), "%s/%s/locales", runtime_dir, res);
    zc_str_set(&settings.locales_dir_path, buf);
#else
    snprintf(buf, sizeof(buf), "%s/Release/Chromium Embedded Framework.framework",
             runtime_dir);
    zc_str_set(&settings.framework_dir_path, buf);
#endif

    cef_main_args_t args;
    zc_main_args(&args);
#ifdef __APPLE__
    /* After zc_load: the CrAppProtocol object comes from the framework, and
     * before initialize: Chromium reaches for NSApp during startup. */
    zc_mac_patch_nsapp();
#endif
    int ok = zc.initialize(&args, &settings, &zc_app, NULL);

    zc_str_free(&settings.log_file);
    zc_str_free(&settings.browser_subprocess_path);
    zc_str_free(&settings.root_cache_path);
    zc_str_free(&settings.cache_path);
    zc_str_free(&settings.locale);
    zc_str_free(&settings.resources_dir_path);
    zc_str_free(&settings.locales_dir_path);
    zc_str_free(&settings.framework_dir_path);

    if (!ok) {
        zc_fail("cef_initialize failed (profile=%s)",
                profile[0] ? profile : "(none)");
        return 0;
    }
    zc_ready = 1;
    return 1;
}

/* One turn of CEF's message loop, driven from the host UI loop. */
ZC_EXPORT void zan_cef_work(void) {
    if (zc_ready && !zc_shut) {
#ifdef _WIN32
        {   /* CEF is driven from the host UI loop, so a host that stops
             * calling here stalls Chromium -- which surfaces as unrelated-
             * looking failures inside CEF ("Timeout of new browser info
             * response"). ZAN_CEF_LOG=1 reports the gaps. */
            static unsigned long long prev = 0, worst = 0;
            static unsigned long calls = 0, slow = 0;
            if (zc_log_on()) {
                unsigned long long now = GetTickCount64();
                if (prev) {
                    unsigned long long gap = now - prev;
                    calls++;
                    if (gap > worst) worst = gap;
                    if (gap > 300) {
                        SYSTEMTIME st;
                        GetLocalTime(&st);
                        slow++;
                        fprintf(stderr,
                                "[pump] %02d:%02d:%02d.%03d gap=%llu ms worst=%llu"
                                " slow=%lu calls=%lu\n",
                                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                                gap, worst, slow, calls);
                        fflush(stderr);
                    }
                }
                prev = now;
            }
        }
#endif
        zc.do_message_loop_work();
    }
}

ZC_EXPORT void zan_cef_shutdown(void) {
    if (!zc_ready || zc_shut) return;
    for (int i = 0; i < ZC_MAX_BROWSERS; i++) {
        zc_browser_t *b = &zc_browsers[i];
        if (b->used && b->browser && !b->gone) {
            cef_browser_host_t *host = b->browser->get_host(b->browser);
            if (host) {
                host->close_browser(host, 1);
                host->base.release(&host->base);
            }
        }
    }
    /* Let the browsers finish closing before the context goes away. */
    for (int i = 0; i < 200; i++) zc.do_message_loop_work();
    zc.shutdown();
    zc_shut = 1;
    zc_ready = 0;
}

/* ---------------------------------------------------------- public: browser */

ZC_EXPORT void *zan_cef_window(int h);

ZC_EXPORT int zan_cef_create(void *parent, int x, int y, int w, int h,
                             const char *url) {
    if (!zan_cef_ready()) {
        zc_fail("cef is not initialized");
        return 0;
    }
    zc_browser_t *b = NULL;
    for (int i = 0; i < ZC_MAX_BROWSERS; i++) {
        if (!zc_browsers[i].used) { b = &zc_browsers[i]; b->id = i + 1; break; }
        if (zc_browsers[i].gone) {   /* reclaim a closed slot */
            b = &zc_browsers[i];
            for (int q = 0; q < ZC_MAX_CDP_QUEUE; q++) {
                free(b->queue[q]);
                b->queue[q] = NULL;
            }
            free(b->taken);
            free(b->clip_spec);
            int id = i + 1;
            memset(b, 0, sizeof(*b));
            b->id = id;
            break;
        }
    }
    if (!b) {
        zc_fail("too many cef browsers (max %d)", ZC_MAX_BROWSERS);
        return 0;
    }
    memset(b->url, 0, sizeof(b->url));
    memset(b->title, 0, sizeof(b->title));
    memset(b->popup_url, 0, sizeof(b->popup_url));
    b->popup_policy = 0;
    b->popup_dropped = 0;
    b->used = 1;
    b->gone = 0;
    b->bx = x;
    b->by = y;
    b->bw = w > 0 ? w : 1;
    b->bh = h > 0 ? h : 1;
    b->shown = -1;
    zc_browser_init_handlers(b);

    cef_window_info_t wi;
    memset(&wi, 0, sizeof(wi));
#ifndef ZAN_CEF_LEGACY
    /* Sized structs arrived with the versioned C API; CEF 109 has no field. */
    wi.size = sizeof(wi);
#endif
    wi.bounds.x = x;
    wi.bounds.y = y;
    wi.bounds.width = w > 0 ? w : 1;
    wi.bounds.height = h > 0 ? h : 1;
    /* A parent means embedding as a child window inside a Zan control tree;
     * without one the browser gets its own top-level window (used by tests and
     * tool windows). Off-screen rendering needs a render handler and arrives
     * with the framebuffer path. */
#ifdef _WIN32
    if (parent) {
        wi.parent_window = (cef_window_handle_t)parent;
        wi.style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    } else {
        wi.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE;
    }
#elif defined(__APPLE__)
    /* The mac window info names the field parent_view: it is an NSView, not a
     * window handle -- and the host handle (zan_gui_create_window) is the
     * NSWindow, so hand Chromium that window's content view. Passing the window
     * itself leaves CEF without a usable parent and every browser comes up as
     * its own top-level window, next to (not inside) the app's own chrome. */
    {
        void *pview = zc_ns_id((void *)parent, "contentView");
        wi.parent_view = (cef_window_handle_t)pview;
        if (zc_log_on()) {
            zc_rect_t pb = zc_ns_rect(pview, "bounds");
            fprintf(stderr, "[zan_cef] embed parent=%p contentView=%p "
                            "bounds=%.0fx%.0f flipped=%d req=%d,%d %dx%d\n",
                    parent, pview, pb.w, pb.h, zc_ns_bool(pview, "isFlipped"),
                    x, y, w, h);
        }
        if (pview) {
            /* AppKit bounds are points; the caller measured pixels. */
            double sc = zc_view_scale(pview);
            wi.bounds.x = (int)((double)wi.bounds.x / sc);
            wi.bounds.y = (int)((double)wi.bounds.y / sc);
            wi.bounds.width = (int)((double)wi.bounds.width / sc);
            wi.bounds.height = (int)((double)wi.bounds.height / sc);
            if (wi.bounds.width < 1) wi.bounds.width = 1;
            if (wi.bounds.height < 1) wi.bounds.height = 1;
        }
    }
#else
    wi.parent_window = (cef_window_handle_t)(uintptr_t)parent;
#endif

    cef_browser_settings_t bs;
    memset(&bs, 0, sizeof(bs));
    bs.size = sizeof(bs);

    cef_string_t curl;
    zc_str_set(&curl, url && url[0] ? url : "about:blank");
    cef_browser_t *browser = zc.create_browser_sync(&wi, &b->client, &curl, &bs,
                                                    NULL, NULL);
    zc_str_free(&curl);
    if (!browser) {
        b->used = 0;
        zc_fail("cef_browser_host_create_browser_sync failed");
        return 0;
    }
    if (b->browser != browser) {
        /* on_after_created already took a reference; keep exactly one. */
        if (b->browser) browser->base.release(&browser->base);
        else b->browser = browser;
    } else {
        browser->base.release(&browser->base);
    }

    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (host) {
        b->cdp_reg = host->add_dev_tools_message_observer(host, &b->observer);
        host->base.release(&host->base);
    }
#ifdef __APPLE__
    if (zc_log_on()) {
        void *wnd = zan_cef_window(b->id);
        zc_rect_t fr = zc_ns_rect(wnd, "frame");
        fprintf(stderr, "[zan_cef] browser view=%p superview=%p "
                        "frame=%.0f,%.0f %.0fx%.0f hidden=%d\n",
                wnd, zc_ns_id(wnd, "superview"), fr.x, fr.y, fr.w, fr.h,
                zc_ns_bool(wnd, "isHidden"));
    }
#endif
    return b->id;
}

ZC_EXPORT void zan_cef_set_visible(int h, int visible);

ZC_EXPORT void zan_cef_close(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    /* close_browser() is asynchronous: the child window lives until CEF gets
     * enough message-loop turns to destroy it, and the view that used to pump
     * the loop every frame is being torn down right now. Hide it up front so a
     * closed tab cannot keep covering the host UI in the meantime. */
    zan_cef_set_visible(h, 0);
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->close_browser(host, 1);
    host->base.release(&host->base);
}

ZC_EXPORT int zan_cef_alive(int h) { return zc_get(h) != NULL; }

/* Native window of the embedded browser, so the host can move/clip it. */
ZC_EXPORT void *zan_cef_window(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return NULL;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return NULL;
    cef_window_handle_t wnd = host->get_window_handle(host);
    host->base.release(&host->base);
    return (void *)(uintptr_t)wnd;
}

ZC_EXPORT void zan_cef_set_bounds(int h, int x, int y, int w, int hh) {
    void *wnd = zan_cef_window(h);
    if (!wnd) return;
    if (w < 1) w = 1;
    if (hh < 1) hh = 1;
    {
        zc_browser_t *bb = zc_get(h);
        if (bb) {
            bb->bx = x; bb->by = y; bb->bw = w; bb->bh = hh;
            /* The clip/mask was built for the old frame: force the next
             * set_clip to rebuild it instead of matching its cached spec. */
            free(bb->clip_spec);
            bb->clip_spec = NULL;
        }
    }
#ifdef _WIN32
    SetWindowPos((HWND)wnd, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
#elif defined(__APPLE__)
    /* Host coordinates are top-left based; an NSView's frame is expressed in
     * its superview's coordinates, which are bottom-left based unless that
     * view is flipped -- so ask the superview instead of assuming. */
    {
        void *sup = zc_ns_id(wnd, "superview");
        double sc = zc_view_scale(sup ? sup : wnd);
        double px = (double)x / sc, pw = (double)w / sc;
        double py = (double)y / sc, ph = (double)hh / sc;
        double top = py;
        if (sup && !zc_ns_bool(sup, "isFlipped")) {
            zc_rect_t pb = zc_ns_rect(sup, "bounds");
            top = pb.h - (py + ph);
        }
        zc_rect_t r;
        r.x = px;
        r.y = top;
        r.w = pw < 1.0 ? 1.0 : pw;
        r.h = ph < 1.0 ? 1.0 : ph;
        zc_ns_set_frame(wnd, r);
        if (zc_log_on()) {
            static int once;
            if (once < 4) {
                once++;
                fprintf(stderr, "[zan_cef] bounds host=%d,%d %dx%d -> "
                                "view=%.0f,%.0f %.0fx%.0f scale=%.1f "
                                "sup=%p flipped=%d\n", x, y, w, hh,
                        r.x, r.y, r.w, r.h, sc, sup,
                        sup ? zc_ns_bool(sup, "isFlipped") : 0);
            }
        }
    }
#else
    /* X11 lives in libcef's process already; resize through the display it
     * owns rather than linking libX11 into this driver. */
    typedef void *(*fn_xdisplay)(void);
    typedef int (*fn_move_resize)(void *, unsigned long, int, int, unsigned, unsigned);
    typedef int (*fn_flush)(void *);
    fn_xdisplay get_display = NULL;
    void *s = zc_sym(zc.lib, "cef_get_xdisplay");
    if (!s) return;
    memcpy(&get_display, &s, sizeof(void *));
    void *dpy = get_display();
    if (!dpy) return;
    void *x11 = zc_open("libX11.so.6");
    if (!x11) return;
    fn_move_resize move_resize = NULL;
    fn_flush flush = NULL;
    void *m = zc_sym(x11, "XMoveResizeWindow");
    void *f = zc_sym(x11, "XFlush");
    if (m && f) {
        memcpy(&move_resize, &m, sizeof(void *));
        memcpy(&flush, &f, sizeof(void *));
        move_resize(dpy, (unsigned long)(uintptr_t)wnd, x, y, (unsigned)w,
                    (unsigned)hh);
        flush(dpy);
    }
#endif
    zc_browser_t *b = zc_get(h);
    if (b && b->browser) {
        cef_browser_host_t *host = b->browser->get_host(b->browser);
        if (host) {
            host->was_resized(host);
            host->base.release(&host->base);
        }
    }
}

ZC_EXPORT void zan_cef_set_visible(int h, int visible) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    /* The host calls this every frame (from set_clip); ShowWindow and
     * WasHidden both end in repaint/notify churn when nothing changed. */
    int state = visible ? 1 : 0;
    if (b->shown == state) return;
    b->shown = state;
    void *wnd = zan_cef_window(h);
#ifdef _WIN32
    if (wnd) ShowWindow((HWND)wnd, visible ? SW_SHOWNA : SW_HIDE);
#elif defined(__APPLE__)
    zc_ns_set_bool(wnd, "setHidden:", visible ? 0 : 1);
#else
    if (wnd) {
        typedef void *(*fn_xdisplay)(void);
        typedef int (*fn_win)(void *, unsigned long);
        void *s = zc_sym(zc.lib, "cef_get_xdisplay");
        void *x11 = zc_open("libX11.so.6");
        if (s && x11) {
            fn_xdisplay get_display = NULL;
            memcpy(&get_display, &s, sizeof(void *));
            void *dpy = get_display();
            void *op = zc_sym(x11, visible ? "XMapWindow" : "XUnmapWindow");
            void *f = zc_sym(x11, "XFlush");
            if (dpy && op && f) {
                fn_win act = NULL, flush = NULL;
                memcpy(&act, &op, sizeof(void *));
                memcpy(&flush, &f, sizeof(void *));
                act(dpy, (unsigned long)(uintptr_t)wnd);
                ((int (*)(void *))flush)(dpy);
            }
        }
    }
#endif
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (host) {
        host->was_hidden(host, visible ? 0 : 1);
        host->base.release(&host->base);
    }
}

#ifdef __APPLE__
/* Punch the host's visible region out of the browser view with a mask layer.
 * Rects arrive in host pixels with a top-left origin; the view's own layer is
 * bottom-left based unless the view is flipped, and its coordinates are points,
 * hence the divide by the backing scale. A single rect covering the whole view
 * drops the mask entirely (no mask = no compositing cost while nothing
 * overlaps the page). */
static void zc_mac_clip(zc_browser_t *b, const char *spec) {
    void *wnd = zan_cef_window(b->id);
    if (!wnd || !zc_cg_init()) return;
    /* Called every frame; rebuilding the mask each time would thrash the
     * compositor, so only act when the region actually changed. */
    if (b->clip_spec && strcmp(b->clip_spec, spec) == 0) return;
    zc_rect_t fr = zc_ns_rect(wnd, "frame");
    if (fr.w <= 0.0 || fr.h <= 0.0) return;
    double sc = zc_view_scale(wnd);
    int flipped = zc_ns_bool(wnd, "isFlipped");
    void *path = zc_cg.path_create();
    if (!path) return;
    int rects = 0, whole = 0;
    const char *p = spec;
    while (*p) {
        char *end = NULL;
        long v[4] = { 0, 0, 0, 0 };
        int n = 0;
        while (n < 4) {
            v[n] = strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            n++;
            if (*p == ',') p++;
            else break;
        }
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
        if (n != 4 || v[2] <= 0 || v[3] <= 0) continue;
        zc_rect_t r;
        r.x = ((double)v[0] - (double)b->bx) / sc;
        r.w = (double)v[2] / sc;
        r.h = (double)v[3] / sc;
        double top = ((double)v[1] - (double)b->by) / sc;
        r.y = flipped ? top : fr.h - top - r.h;
        zc_cg.path_add_rect(path, NULL, r);
        rects++;
        if (r.x <= 0.0 && r.y <= 0.0 && r.x + r.w >= fr.w
            && r.y + r.h >= fr.h) {
            whole = 1;
        }
    }
    if (rects == 0) {
        zc_cg.path_release(path);
        return;
    }
    zc_ns_set_bool(wnd, "setWantsLayer:", 1);
    void *layer = zc_ns_id(wnd, "layer");
    if (layer) {
        if (rects == 1 && whole) {
            zc_ns_set_id(layer, "setMask:", NULL);
        } else {
            void *shape = zc_ns_id(zc_ns_class("CAShapeLayer"), "layer");
            if (shape) {
                zc_ns_set_frame(shape, zc_ns_rect(layer, "bounds"));
                zc_ns_set_id(shape, "setPath:", path);
                zc_ns_set_id(layer, "setMask:", shape);
            }
        }
    }
    zc_cg.path_release(path);
    free(b->clip_spec);
    b->clip_spec = zc_dup(spec, strlen(spec));
}
#endif

/* Apply the host's per-frame occlusion result: `spec` is a "x,y,w,h;..." union
 * of visible rectangles in host coordinates ("" = fully covered). Windows clip
 * the child window to that region, macOS masks the browser view's layer. */
ZC_EXPORT void zan_cef_set_clip(int h, const char *spec) {
    zc_browser_t *b = zc_get(h);
    if (!b) return;
    int visible = spec && spec[0];
    if (zc_log_on()) {
        static int once;
        if (once < 4) {
            once++;
            fprintf(stderr, "[zan_cef] clip h=%d visible=%d spec=%s\n", h,
                    visible, spec ? spec : "(null)");
        }
    }
#ifdef _WIN32
    void *wnd = zan_cef_window(h);
    /* SetWindowRgn repaints the child window even when the region is
     * identical, and the host calls this every frame -- without the guard
     * the page flickers (the WebView2 backend and the macOS mask path both
     * skip when the spec is unchanged). */
    if (visible && wnd
        && (!b->clip_spec || strcmp(b->clip_spec, spec) != 0)) {
        HRGN total = CreateRectRgn(0, 0, 0, 0);
        const char *p = spec;
        while (*p) {
            int v[4] = { 0, 0, 0, 0 };
            int n = 0;
            while (n < 4 && *p) {
                int sign = 1;
                if (*p == '-') { sign = -1; p++; }
                int acc = 0, digits = 0;
                while (*p >= '0' && *p <= '9') {
                    acc = acc * 10 + (*p - '0');
                    p++;
                    digits++;
                }
                if (!digits) break;
                v[n++] = sign * acc;
                if (*p == ',' || *p == ';') p++;
                else break;
            }
            if (n == 4 && v[2] > 0 && v[3] > 0) {
                HRGN r = CreateRectRgn(v[0] - b->bx, v[1] - b->by,
                                       v[0] - b->bx + v[2],
                                       v[1] - b->by + v[3]);
                CombineRgn(total, total, r, RGN_OR);
                DeleteObject(r);
            }
            while (*p && *p != ';' && (*p < '0' || *p > '9') && *p != '-') p++;
        }
        /* SetWindowRgn takes ownership of the region. */
        SetWindowRgn((HWND)wnd, total, TRUE);
        free(b->clip_spec);
        b->clip_spec = zc_dup(spec, strlen(spec));
    }
#elif defined(__APPLE__)
    /* The browser is an NSView sibling of the host's software canvas, so it
     * always draws over it: menus, dropdowns and dialogs above the page can
     * only be honoured by punching them out of the view itself. Same treatment
     * as the WKWebView backend: the visible union becomes a mask layer. */
    if (visible) zc_mac_clip(b, spec);
#endif
    zan_cef_set_visible(h, visible);
}

ZC_EXPORT void zan_cef_set_focus(int h, int focus) {
    zc_browser_t *b = zc_get(h);
    if (!b) return;
    /* Latch first: focus=0 means "the host owns the keyboard now", which must
     * hold even before the browser exists (or after it went away), otherwise
     * the first navigation grabs the focus back. */
    b->host_focus = focus ? 0 : 1;
    if (!b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->set_focus(host, focus);
    host->base.release(&host->base);
}

/* --------------------------------------------------- zoom / find / print --- */

/* Chromium's zoom is logarithmic: level 0 is 100%, +1 is a factor of 1.2. */
ZC_EXPORT void zan_cef_set_zoom(int h, double level) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->set_zoom_level(host, level);
    host->base.release(&host->base);
}

ZC_EXPORT double zan_cef_get_zoom(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return 0.0;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return 0.0;
    double level = host->get_zoom_level(host);
    host->base.release(&host->base);
    return level;
}

/* In-page search. find_next=0 starts a new search, 1 steps through the
 * matches of the current one. */
ZC_EXPORT void zan_cef_find(int h, const char *text, int forward,
                            int match_case, int find_next) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser || !text) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    cef_string_t s;
    zc_str_set(&s, text);
    /* Same shape on both branches: 109 had already dropped the
     * caller-assigned search identifier. */
    host->find(host, &s, forward, match_case, find_next);
    zc_str_free(&s);
    host->base.release(&host->base);
}

ZC_EXPORT void zan_cef_stop_find(int h, int clear_selection) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->stop_finding(host, clear_selection);
    host->base.release(&host->base);
}

/* Opens the platform print dialog (printing to PDF without a dialog is
 * CDP's Page.printToPDF, which also returns the bytes). */
ZC_EXPORT void zan_cef_print(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->print(host);
    host->base.release(&host->base);
}

/* DevTools in its own CEF-owned window; the optional arguments are all NULL,
 * which is how CEF is told to use its defaults. */
ZC_EXPORT void zan_cef_show_devtools(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->show_dev_tools(host, NULL, NULL, NULL, NULL);
    host->base.release(&host->base);
}

ZC_EXPORT void zan_cef_close_devtools(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->close_dev_tools(host);
    host->base.release(&host->base);
}

/* ------------------------------------------------------------- navigation --- */

ZC_EXPORT void zan_cef_navigate(int h, const char *url) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser || !url) return;
    cef_frame_t *frame = b->browser->get_main_frame(b->browser);
    if (!frame) return;
    cef_string_t s;
    zc_str_set(&s, url);
    frame->load_url(frame, &s);
    zc_str_free(&s);
    frame->base.release(&frame->base);
}

ZC_EXPORT void zan_cef_back(int h) {
    zc_browser_t *b = zc_get(h);
    if (b && b->browser) b->browser->go_back(b->browser);
}

ZC_EXPORT void zan_cef_forward(int h) {
    zc_browser_t *b = zc_get(h);
    if (b && b->browser) b->browser->go_forward(b->browser);
}

ZC_EXPORT void zan_cef_reload(int h, int ignore_cache) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    if (ignore_cache) b->browser->reload_ignore_cache(b->browser);
    else b->browser->reload(b->browser);
}

ZC_EXPORT void zan_cef_stop(int h) {
    zc_browser_t *b = zc_get(h);
    if (b && b->browser) b->browser->stop_load(b->browser);
}

ZC_EXPORT int zan_cef_can_go_back(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->can_back : 0;
}

ZC_EXPORT int zan_cef_can_go_forward(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->can_forward : 0;
}

ZC_EXPORT int zan_cef_is_loading(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->loading : 0;
}

ZC_EXPORT int zan_cef_nav_seq(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->nav_seq : 0;
}

ZC_EXPORT int zan_cef_last_status(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->last_status : 0;
}

ZC_EXPORT int zan_cef_last_error_code(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->last_error : 0;
}

ZC_EXPORT const char *zan_cef_url(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b) return "";
    if (!b->url[0] && b->browser) {
        cef_frame_t *frame = b->browser->get_main_frame(b->browser);
        if (frame) {
            zc_userfree_get(frame->get_url(frame), b->url, sizeof(b->url));
            frame->base.release(&frame->base);
        }
    }
    return b->url;
}

ZC_EXPORT const char *zan_cef_title(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->title : "";
}

/* Fire-and-forget JavaScript; use CDP Runtime.evaluate when the result or an
 * exception matters. */
ZC_EXPORT void zan_cef_execute_js(int h, const char *code, const char *script_url,
                                  int start_line) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser || !code) return;
    cef_frame_t *frame = b->browser->get_main_frame(b->browser);
    if (!frame) return;
    cef_string_t c, u;
    zc_str_set(&c, code);
    zc_str_set(&u, script_url ? script_url : "");
    frame->execute_java_script(frame, &c, &u, start_line);
    zc_str_free(&c);
    zc_str_free(&u);
    frame->base.release(&frame->base);
}

/* -------------------------------------------------------------- public: CDP */

/* Send a raw DevTools protocol message. `params_json` may be empty; the
 * allocated message id is returned (0 on failure) and every reply/event shows
 * up through zan_cef_cdp_take. */
ZC_EXPORT int zan_cef_cdp_send(int h, const char *method, const char *params_json) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser || !method || !method[0]) return 0;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return 0;
    int id = ++b->cdp_id;
    int has_params = params_json && params_json[0];
    size_t need = strlen(method) + (has_params ? strlen(params_json) : 0) + 64;
    char *msg = (char *)malloc(need);
    if (!msg) {
        host->base.release(&host->base);
        return 0;
    }
    if (has_params)
        snprintf(msg, need, "{\"id\":%d,\"method\":\"%s\",\"params\":%s}", id,
                 method, params_json);
    else
        snprintf(msg, need, "{\"id\":%d,\"method\":\"%s\"}", id, method);
    int ok = host->send_dev_tools_message(host, msg, strlen(msg));
    free(msg);
    host->base.release(&host->base);
    return ok ? id : 0;
}

/* window.open / target=_blank policy: 0 = CEF's own popup window (default),
 * 1 = block, 2 = cancel and hand the URL to the host via
 * zan_cef_take_popup_url. */
ZC_EXPORT void zan_cef_set_popup_policy(int h, int policy) {
    zc_browser_t *b = zc_get(h);
    if (b) b->popup_policy = policy;
}

/* Pending popup target under policy 2, "" when there is none. The returned
 * pointer stays valid until the next take on the same browser. */
ZC_EXPORT const char *zan_cef_take_popup_url(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b) return "";
    zc_enter();
    if (!b->popup_url[0]) {
        zc_leave();
        return "";
    }
    free(b->taken_popup);
    b->taken_popup = zc_dup(b->popup_url, strlen(b->popup_url));
    b->popup_url[0] = '\0';
    zc_leave();
    return b->taken_popup ? b->taken_popup : "";
}

ZC_EXPORT int zan_cef_cdp_pending(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b) return 0;
    zc_enter();
    int n = b->q_count;
    zc_leave();
    return n;
}

ZC_EXPORT int zan_cef_cdp_dropped(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->q_dropped : 0;
}

ZC_EXPORT int zan_cef_cdp_attached(int h) {
    zc_browser_t *b = zc_get(h);
    return b ? b->cdp_attached : 0;
}

/* Oldest queued DevTools message, or "" when the queue is empty. The returned
 * pointer stays valid until the next take on the same browser. */
ZC_EXPORT const char *zan_cef_cdp_take(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b) return "";
    zc_enter();
    char *msg = NULL;
    if (b->q_count > 0) {
        msg = b->queue[b->q_head];
        b->queue[b->q_head] = NULL;
        b->q_head = (b->q_head + 1) % ZC_MAX_CDP_QUEUE;
        b->q_count--;
    }
    zc_leave();
    if (!msg) return "";
    free(b->taken);
    b->taken = msg;
    return b->taken;
}
