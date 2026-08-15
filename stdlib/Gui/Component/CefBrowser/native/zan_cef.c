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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_command_line_capi.h"
#include "include/capi/cef_devtools_message_observer_capi.h"
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

/* ---------------------------------------------------------------- utilities */

#define ZC_MAX_BROWSERS 64
#define ZC_MAX_CDP_QUEUE 512

static char zc_error[512];

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
typedef int (*zc_fn_utf16_to_utf8)(const char16_t *, size_t, cef_string_utf8_t *);
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
    return (void *)LoadLibraryA(path);
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
    cef_dev_tools_message_observer_t observer;

    char url[2048];
    char title[512];
    int loading, can_back, can_forward;
    int nav_seq, last_status, last_error;
    int closing, gone;
    int cdp_id;                 /* last allocated CDP message id */
    int cdp_attached;

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

static void zc_browser_init_handlers(zc_browser_t *b) {
    memset(&b->client, 0, sizeof(b->client));
    memset(&b->life, 0, sizeof(b->life));
    memset(&b->load, 0, sizeof(b->load));
    memset(&b->display, 0, sizeof(b->display));
    memset(&b->observer, 0, sizeof(b->observer));

    zc_base_init(&b->client.base, sizeof(b->client));
    b->client.get_life_span_handler = zc_get_life_span_handler;
    b->client.get_load_handler = zc_get_load_handler;
    b->client.get_display_handler = zc_get_display_handler;

    zc_base_init(&b->life.base, sizeof(b->life));
    b->life.on_after_created = zc_on_after_created;
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

static void CEF_CALLBACK zc_on_before_command_line_processing(
        cef_app_t *self, const cef_string_t *process_type,
        cef_command_line_t *command_line) {
    (void)self; (void)process_type;
    if (!command_line) return;
    cef_string_t k, v;
    zc_str_set(&k, "autoplay-policy");
    zc_str_set(&v, "no-user-gesture-required");
    command_line->append_switch_with_value(command_line, &k, &v);
    zc_str_free(&k);
    zc_str_free(&v);

    /* "a=b,c" style list handed down from Zan (CefRuntime/CefBrowser). */
    const char *p = zc_extra_switches;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < 512) {
            char item[512];
            memcpy(item, p, len);
            item[len] = '\0';
            char *eq = strchr(item, '=');
            if (eq) {
                *eq = '\0';
                zc_str_set(&k, item);
                zc_str_set(&v, eq + 1);
                command_line->append_switch_with_value(command_line, &k, &v);
                zc_str_free(&k);
                zc_str_free(&v);
            } else {
                zc_str_set(&k, item);
                command_line->append_switch(command_line, &k);
                zc_str_free(&k);
            }
        }
        if (!comma) break;
        p = comma + 1;
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
 * kernel (/proc/self/cmdline on Linux). */
#ifndef _WIN32
static char *zc_argv_storage;
static char *zc_argv[64];
static int zc_argc;

static void zc_read_cmdline(void) {
    if (zc_argc) return;
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) {
        static char fallback[] = "zan";
        zc_argv[0] = fallback;
        zc_argc = 1;
        return;
    }
    size_t cap = 65536, len = 0;
    zc_argv_storage = (char *)malloc(cap);
    if (zc_argv_storage) len = fread(zc_argv_storage, 1, cap - 1, f);
    fclose(f);
    if (!zc_argv_storage || len == 0) {
        static char fallback[] = "zan";
        zc_argv[0] = fallback;
        zc_argc = 1;
        return;
    }
    zc_argv_storage[len] = '\0';
    size_t i = 0;
    while (i < len && zc_argc < 63) {
        zc_argv[zc_argc++] = zc_argv_storage + i;
        i += strlen(zc_argv_storage + i) + 1;
    }
    zc_argv[zc_argc] = NULL;
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
 * calls this as the very first thing in Main. */
ZC_EXPORT int zan_cef_execute_process(const char *runtime_dir) {
    if (!zc_load(runtime_dir)) return -1;
    zc_app_init();
    cef_main_args_t args;
    zc_main_args(&args);
    return zc.execute_process(&args, &zc_app, NULL);
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

    cef_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.size = sizeof(settings);
    settings.no_sandbox = 1;
    settings.multi_threaded_message_loop = 0;
    settings.external_message_pump = 0;
    settings.windowless_rendering_enabled = windowless ? 1 : 0;
    settings.log_severity = LOGSEVERITY_WARNING;

    char buf[1200];
    if (helper_path && helper_path[0])
        zc_str_set(&settings.browser_subprocess_path, helper_path);
    if (cache_path && cache_path[0]) {
        zc_str_set(&settings.root_cache_path, cache_path);
        zc_str_set(&settings.cache_path, cache_path);
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
    int ok = zc.initialize(&args, &settings, &zc_app, NULL);

    zc_str_free(&settings.browser_subprocess_path);
    zc_str_free(&settings.root_cache_path);
    zc_str_free(&settings.cache_path);
    zc_str_free(&settings.locale);
    zc_str_free(&settings.resources_dir_path);
    zc_str_free(&settings.locales_dir_path);
    zc_str_free(&settings.framework_dir_path);

    if (!ok) {
        zc_fail("cef_initialize failed");
        return 0;
    }
    zc_ready = 1;
    return 1;
}

/* One turn of CEF's message loop, driven from the host UI loop. */
ZC_EXPORT void zan_cef_work(void) {
    if (zc_ready && !zc_shut) zc.do_message_loop_work();
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
    b->used = 1;
    b->gone = 0;
    zc_browser_init_handlers(b);

    cef_window_info_t wi;
    memset(&wi, 0, sizeof(wi));
    wi.size = sizeof(wi);
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
    return b->id;
}

ZC_EXPORT void zan_cef_close(int h) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
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
#ifdef _WIN32
    SetWindowPos((HWND)wnd, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
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
    void *wnd = zan_cef_window(h);
#ifdef _WIN32
    if (wnd) ShowWindow((HWND)wnd, visible ? SW_SHOWNA : SW_HIDE);
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

ZC_EXPORT void zan_cef_set_focus(int h, int focus) {
    zc_browser_t *b = zc_get(h);
    if (!b || !b->browser) return;
    cef_browser_host_t *host = b->browser->get_host(b->browser);
    if (!host) return;
    host->set_focus(host, focus);
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
