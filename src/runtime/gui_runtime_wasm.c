/* gui_runtime_wasm.c -- wasm32 browser window shell (ZAN_GUI_WASM).
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 *
 * One canvas = one window, the OHOS single-surface model. The wasm module
 * runs inside a Web Worker and every OS service arrives as a host import
 * in module "zan_env": wasm-ld keeps function imports declared with
 * import_module/import_name (data imports are not supported, so nothing
 * here imports memory), and the JS host supplies them at instantiation.
 * The browser main thread never touches wasm memory -- it writes input
 * into a small SharedArrayBuffer ring and receives finished frames the
 * worker copies out of linear memory. Atomics.wait on the ring's doorbell
 * gives wait_event_timeout a real blocking sleep, so the Zan run loop
 * idles like the desktop shells do in poll() instead of hot-spinning the
 * worker (a wasm call cannot yield, which rules out driving frames from
 * the main thread's rAF).
 *
 * Event ABI is the flat 8-slot contract the OHOS/X11 shells use:
 * e[0] kind, e[1] x, e[2] y, e[3] button, e[4] code, e[5] mods, e[6] flag.
 * Kinds: 0 wake, 1 move, 2 down, 3 up, 4 keydown, 5 keyup, 6 char,
 * 7 resize (x=w y=h), 8 close, 9 kill focus, 13 wheel (+-120), 14 attached.
 * mods: bit0 ctrl, bit1 shift, bit2 alt (Win32 encoding), matching
 * gui_runtime_x11.c's translation. Browser key events already carry
 * Win32-flavored keyCode values for letters/digits/arrows, so the host
 * forwards e.keyCode verbatim into the code slot.
 * ======================================================================== */

#ifdef __wasm__

#include <string.h>
#include <time.h>

#include "../common/zan_abi.h"

/* ---- host imports (module "zan_env") -----------------------------------
 * pump: drain the SharedArrayBuffer ring, feeding zan_gui_wasm_feed per
 *       event. Never blocks.
 * wait: pump, block up to ms on the doorbell (Atomics.wait), pump again;
 *       returns 1 when events are pending in the C ring afterwards.
 * sleep: block ms without pumping (the desktop nanosleep counterpart).
 * present: hand one frame to the host -- pixels at ptr are the surface's
 *       little-endian ARGB (bytes B,G,R,A), w*h*4 bytes.
 * title: NUL-terminated UTF-8, for document.title. */
__attribute__((import_module("zan_env"), import_name("pump")))
static void zan__env_pump(void);
__attribute__((import_module("zan_env"), import_name("wait")))
static i32 zan__env_wait(i32 ms);
__attribute__((import_module("zan_env"), import_name("sleep")))
static void zan__env_sleep(i32 ms);
__attribute__((import_module("zan_env"), import_name("present")))
static void zan__env_present(i32 ptr, i32 w, i32 h);
__attribute__((import_module("zan_env"), import_name("title")))
static void zan__env_title(const char *text, i32 len);

/* ---- window record ----------------------------------------------------- */

typedef struct {
    int w, h;      /* canvas size, device pixels (host decides it) */
    int shown;
    int closed;    /* kind 8 seen: present becomes a no-op */
} zan_wasm_win_t;
static zan_wasm_win_t g_wwin;
#define ZAN_WASM_HWND ((i64)(intptr_t)&g_wwin)

static int  g_window_width  = 0;
static int  g_window_height = 0;

/* ---- event ring -------------------------------------------------------- */

typedef struct { int e[8]; i64 win; } zan_wev_t;
#define ZAN_WQ_CAP 512
static zan_wev_t g_wq[ZAN_WQ_CAP];
static int g_wq_head = 0, g_wq_tail = 0;

static int g_pending_event[8];
static i64 g_event_win = 0;
static long long g_ev_seq = 0;

/* Plain moves coalesce (freshest x/y wins) and wheel floods coalesce by
 * SUMMING deltas -- same contract as the OHOS/SDL shells. Single thread
 * (the worker), so unlike OHOS no lock wraps the ring: the only pushers
 * are the host pump and UiDriver's inject_event, both on this thread. */
static void wq_push(int kind, int x, int y, int button, int code, int mods,
                    int flag) {
    if (kind == 7) { /* resize carries the new canvas size */
        g_window_width = x;
        g_window_height = y;
    }
    int last = (g_wq_tail + ZAN_WQ_CAP - 1) % ZAN_WQ_CAP;
    int has_last = (g_wq_head != g_wq_tail);
    if (kind == 1 && has_last && g_wq[last].e[0] == 1) {
        g_wq[last].e[1] = x; g_wq[last].e[2] = y;
        return;
    }
    if (kind == 13 && has_last && g_wq[last].e[0] == 13) {
        g_wq[last].e[1] = x; g_wq[last].e[2] = y;
        g_wq[last].e[4] += code;
        return;
    }
    int next = (g_wq_tail + 1) % ZAN_WQ_CAP;
    if (next == g_wq_head) return; /* full: drop */
    zan_wev_t *z = &g_wq[g_wq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = flag; z->e[7] = 0;
    z->win = ZAN_WASM_HWND;
    g_wq_tail = next;
}

/* Host -> C injection: one event from the SharedArrayBuffer ring. Returns
 * the ring depth after the push so the JS pump can answer "is anything
 * pending" with one call (the wait import's return path). Exported from
 * the module: zanc adds --export for it when linking a GUI program. */
EXPORT i32 zan_gui_wasm_feed(i32 kind, i32 x, i32 y, i32 button, i32 code,
                             i32 mods, i32 flag) {
    wq_push((int)kind, (int)x, (int)y, (int)button, (int)code, (int)mods,
            (int)flag);
    return (g_wq_tail - g_wq_head + ZAN_WQ_CAP) % ZAN_WQ_CAP;
}

static int wq_pop(void) {
    if (g_wq_head == g_wq_tail) return 0;
    zan_wev_t *z = &g_wq[g_wq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_wq_head = (g_wq_head + 1) % ZAN_WQ_CAP;
    g_ev_seq++;
    return 1;
}

/* ---- window management (browser: no chrome) ---------------------------- */

/* The canvas decides the size; the host seeds kind 7 + 14 before _start so
 * the first poll lays the app out at the real canvas size and repaints it
 * whole (same attach protocol as the OHOS XComponent shell). */
EXPORT i64 zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title;
    g_wwin.w = (int)width;
    g_wwin.h = (int)height;
    return ZAN_WASM_HWND;
}
EXPORT i32 zan_gui_show_window(i64 hwnd_val)     { (void)hwnd_val; g_wwin.shown = 1; return 0; }
EXPORT i32 zan_gui_minimize(i64 hwnd_val)        { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_toggle_maximize(i64 hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_close_window(i64 hwnd_val) {
    (void)hwnd_val;
    g_wwin.closed = 1;
    wq_push(8, 0, 0, 0, 0, 0, 0);
    return 0;
}
EXPORT i32 zan_gui_destroy_window(i64 hwnd_val) {
    (void)hwnd_val;
    g_wwin.shown = 0;
    return 0;
}
EXPORT i32 zan_gui_is_maximized(i64 hwnd_val)     { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_window_visible(i64 hwnd_val)   { (void)hwnd_val; return g_wwin.shown; }
EXPORT i32 zan_gui_window_focused(i64 hwnd_val)   { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_titlebar_height(void)           { return 0; }
EXPORT i32 zan_gui_caption_button_width(void)      { return 0; }
EXPORT i32 zan_gui_set_caption_buttons(i64 h, i32 n) { (void)h; (void)n; return 0; }
EXPORT i32 zan_gui_set_window_pos(i64 h, i32 x, i32 y) { (void)h; (void)x; (void)y; return 0; }
EXPORT i32 zan_gui_center_window(i64 hwnd_val)    { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_set_topmost(i64 h, i32 on)     { (void)h; (void)on; return 0; }
EXPORT i32 zan_gui_set_title(i64 h, const char *t) {
    (void)h;
    if (t) zan__env_title(t, (i32)strlen(t));
    return 0;
}
EXPORT i32 zan_gui_set_cursor(i32 cursor_type)     { (void)cursor_type; return 0; }

/* ---- event pump --------------------------------------------------------
 * Return contract mirrors the SDL driver -- poll 0 = delivered / 1 = empty;
 * wait 0 = delivered; wait_event_timeout 0 = delivered / 1 = timed out.
 * Host events reach the C ring only through the pump import, so every
 * entry point pumps first; UiDriver's inject_event feeds the ring directly
 * (same thread) and needs no pump. */
EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    zan__env_pump();
    if (wq_pop()) return 0;
    return 1;
}

EXPORT i32 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    for (;;) {
        if (wq_pop()) return 0;
        zan__env_pump();
        if (wq_pop()) return 0;
        zan__env_wait(4000);
    }
}

EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (wq_pop()) return 0;
    zan__env_pump();
    if (wq_pop()) return 0;
    if (ms > 0 && zan__env_wait(ms)) {
        /* wait pumped on wake; drain what arrived inside the window */
        if (wq_pop()) return 0;
    }
    return 1;
}

EXPORT i32 zan_gui_wake(void) { return 0; }

EXPORT i32 zan_gui_inject_event(
    i64 hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    (void)hwnd_val;
    wq_push((int)kind, (int)x, (int)y, (int)button, (int)keycode, (int)mods, 0);
    return 0;
}
EXPORT i32 zan_gui_inject_pending(void) {
    return (g_wq_tail - g_wq_head + ZAN_WQ_CAP) % ZAN_WQ_CAP;
}

EXPORT i32 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_seq(void)     { return g_ev_seq; }
EXPORT i32 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i32 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i32 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i32 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i32 zan_gui_event_mods(void)    { return g_pending_event[5]; }
EXPORT i32 zan_gui_event_flag(void)    { return g_pending_event[6]; }
EXPORT i64 zan_gui_event_hwnd(void)   { return g_event_win; }

EXPORT i32 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i32 zan_gui_window_height(void) { return g_window_height; }
EXPORT i32 zan_gui_client_width(i64 hwnd_val)  { (void)hwnd_val; return g_window_width; }
EXPORT i32 zan_gui_client_height(i64 hwnd_val) { (void)hwnd_val; return g_window_height; }

/* ---- present -----------------------------------------------------------
 * Dirty rects are tracked for API parity but v1 ships whole frames: the
 * host copy out of linear memory is one memcpy-shaped pass and correctness
 * beats bandwidth for the first browser milestone. */
#define ZAN_DIRTY_MAX_WASM 512
static int g_dirty_wasm[ZAN_DIRTY_MAX_WASM * 4];
static int g_dirty_count_wasm;
static int g_dirty_overflow_wasm;

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_dirty_count_wasm >= ZAN_DIRTY_MAX_WASM) { g_dirty_overflow_wasm = 1; return 0; }
    g_dirty_wasm[g_dirty_count_wasm * 4 + 0] = (int)x;
    g_dirty_wasm[g_dirty_count_wasm * 4 + 1] = (int)y;
    g_dirty_wasm[g_dirty_count_wasm * 4 + 2] = (int)w;
    g_dirty_wasm[g_dirty_count_wasm * 4 + 3] = (int)h;
    g_dirty_count_wasm++;
    return 0;
}

EXPORT void zan_gui_present_full(void) {
    g_dirty_count_wasm = 0;
    g_dirty_overflow_wasm = 0;
}

EXPORT i32 zan_gui_present(i64 hwnd_val, i32 surface_id) {
    (void)hwnd_val;
    if (g_wwin.closed) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id])
        return 1;
    zan_surface_t *s = g_surfaces[surface_id];
    /* A backend holding the frame elsewhere has to put it back first. */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    zan__env_present((i32)(intptr_t)s->pixels, s->width, s->height);
    g_dirty_count_wasm = 0;
    g_dirty_overflow_wasm = 0;
    return 0;
}

/* ---- platform services ------------------------------------------------- */

/* The canvas is sized 1:1 in CSS pixels by the host, so desktop-96 scale
 * until a devicePixelRatio-aware host lands. */
EXPORT i32 zan_gui_get_dpi_scale(void) { return 100; }

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* wasm has no nanosleep: the wasi clock import only reads time. Blocking
 * goes through the host (Atomics.wait is legal on the worker thread). */
EXPORT void zan_gui_sleep_ms(i32 ms) {
    if (ms > 0) zan__env_sleep(ms);
}

EXPORT i32 zan_gui_set_clipboard(const char *utf8) { (void)utf8; return 1; }
EXPORT const char *zan_gui_get_clipboard(void)     { return ""; }
EXPORT int  zan_gui_drop_pending(void)             { return 0; }
EXPORT const char *zan_gui_drop_take(void)         { return ""; }
EXPORT void zan_gui_set_ime_pos(i32 x, i32 y)      { (void)x; (void)y; }
EXPORT void zan_gui_set_ime_open(i32 on)           { (void)on; }
EXPORT i32 zan_gui_ime_composing(void)             { return 0; }
EXPORT i32 zan_gui_enable_glass(i64 hwnd_val, i32 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1;
}
EXPORT i32 zan_gui_disable_glass(i64 hwnd_val)    { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_set_opacity(i64 h, i32 percent) { (void)h; (void)percent; return 0; }

/* Files: wasi preopens give the module its own snapshot-scoped FS; there is
 * no Android-style app dir. Consumers of android_files_dir fall back to
 * relative paths, which resolve against the preopen root. */
EXPORT const char *zan_gui_android_files_dir(void) { return "."; }

/* ---- SDL-compat / scene / webview stubs --------------------------------
 * Same semantics as the other non-SDL shells: adopt/scene return "cannot"
 * (1) so the Zan side keeps the software path, webview_create returns 0 so
 * WebViewBackend paints its in-canvas placeholder instead of embedding. */
EXPORT i32 zan_gui_adopt_sdl_window(i64 hwnd_val)              { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_scene_set_renderer(i64 hwnd_val, i64 rend) { (void)hwnd_val; (void)rend; return 1; }
EXPORT i32 zan_gui_scene_upload(i64 hwnd_val, const void *bgra, i32 w, i32 h) {
    (void)hwnd_val; (void)bgra; (void)w; (void)h; return 1;
}
EXPORT i32 zan_gui_scene_present(i64 hwnd_val, i32 surface_id) { (void)hwnd_val; (void)surface_id; return 1; }

EXPORT i32 zan_gui_webview_create(i64 hwnd, const char *profile_id) {
    (void)hwnd; (void)profile_id; return 0;
}
EXPORT void zan_gui_webview_destroy(i32 h) { (void)h; }
EXPORT void zan_gui_webview_set_frame(i32 h, i32 x, i32 y, i32 w, i32 hh) {
    (void)h; (void)x; (void)y; (void)w; (void)hh;
}
EXPORT void zan_gui_webview_set_visible(i32 h, i32 visible) { (void)h; (void)visible; }
EXPORT void zan_gui_webview_set_clip(i32 h, const char *spec) { (void)h; (void)spec; }
EXPORT void zan_gui_webview_navigate(i32 h, const char *url) { (void)h; (void)url; }
EXPORT void zan_gui_webview_load_html(i32 h, const char *html, const char *base_url) {
    (void)h; (void)html; (void)base_url;
}
EXPORT void zan_gui_webview_back(i32 h)    { (void)h; }
EXPORT void zan_gui_webview_forward(i32 h) { (void)h; }
EXPORT void zan_gui_webview_reload(i32 h)  { (void)h; }
EXPORT void zan_gui_webview_stop(i32 h)    { (void)h; }
EXPORT i32 zan_gui_webview_can_go_back(i32 h)    { (void)h; return 0; }
EXPORT i32 zan_gui_webview_can_go_forward(i32 h) { (void)h; return 0; }
EXPORT i32 zan_gui_webview_is_loading(i32 h)     { (void)h; return 0; }
EXPORT i32 zan_gui_webview_nav_seq(i32 h)        { (void)h; return 0; }
EXPORT i32 zan_gui_webview_last_status(i32 h)    { (void)h; return 0; }
EXPORT const char *zan_gui_webview_get_url(i32 h)   { (void)h; return ""; }
EXPORT const char *zan_gui_webview_get_title(i32 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_last_request(i32 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_eval(i32 h, const char *js) {
    (void)h; (void)js; return "";
}
EXPORT const char *zan_gui_webview_get_cookies(i32 h, const char *url) {
    (void)h; (void)url; return "";
}
EXPORT void zan_gui_webview_set_cookie(i32 h, const char *url, const char *name,
                                       const char *value) {
    (void)h; (void)url; (void)name; (void)value;
}
EXPORT void zan_gui_webview_clear_cookies(i32 h) { (void)h; }
EXPORT void zan_gui_webview_set_devtools_enabled(i32 h, i32 enabled) {
    (void)h; (void)enabled;
}
EXPORT void zan_gui_webview_set_context_menu_enabled(i32 h, i32 enabled) {
    (void)h; (void)enabled;
}

/* ---- UI dispatch (Gui.Dispatcher's queue) ------------------------------
 * The desktop implementation lives in rt_sync.c behind a mutex; wasm32 has
 * one thread and rt_sync cannot build here (pthread/shm), so the same queue
 * with the same ABI and retain/release discipline runs lock-free in the
 * gui object. Every pusher and drainer is this thread, so plain statics
 * stand in for the lock; the only observable difference is that "thread-
 * safe post" marshals nothing -- on wasm there is nowhere to marshal from.
 * Delegate values are tagged closure records (ZAN_CLOSURE_* in zan_abi.h):
 * the queue owns one reference, post retains, take transfers it to the
 * caller, clear releases in place -- exactly rt_sync.c's contract. */
#define ZAN_WDISP_CAP 4096
static void *g_wdisp[ZAN_WDISP_CAP];
static int g_wdisp_head = 0, g_wdisp_tail = 0;

static void *wdisp_record(void *d) {
    uintptr_t v = (uintptr_t)d;
    if (!(v & (uintptr_t)ZAN_CLOSURE_TAG)) return NULL;
    return (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

static void wdisp_retain(void *d) {
    void *rec = wdisp_record(d);
    if (!rec) return;
    *(int64_t *)((char *)rec + ZAN_OBJ_RC_OFF) += 1;
}

static void wdisp_release(void *d) {
    void *rec = wdisp_record(d);
    if (!rec) return;
    /* zan_abi.h's closure offsets describe the native 64-bit record; wasm32
     * pointers are 4 bytes, so the {fn, dtor, target} prefix sits at 0/4/8
     * (offset 0 is the only layout-invariant one). */
    void *dtor = *(void **)((char *)rec + 1 * sizeof(void *));
    if (dtor) ((void (*)(void *))dtor)(rec);
}

void zan_dispatch_init(void) {
    g_wdisp_head = 0;
    g_wdisp_tail = 0;
}

int32_t zan_dispatch_post(void *fn) {
    if (!fn) return 0;
    int next = (g_wdisp_tail + 1) % ZAN_WDISP_CAP;
    if (next == g_wdisp_head) return 0; /* full: dropped, like the ceiling */
    wdisp_retain(fn);
    g_wdisp[g_wdisp_tail] = fn;
    g_wdisp_tail = next;
    return 1;
}

void *zan_dispatch_take(void) {
    if (g_wdisp_head == g_wdisp_tail) return NULL;
    void *fn = g_wdisp[g_wdisp_head];
    g_wdisp_head = (g_wdisp_head + 1) % ZAN_WDISP_CAP;
    return fn;
}

void zan_dispatch_clear(void) {
    while (g_wdisp_head != g_wdisp_tail) {
        void *d = g_wdisp[g_wdisp_head];
        g_wdisp_head = (g_wdisp_head + 1) % ZAN_WDISP_CAP;
        wdisp_release(d);
    }
    g_wdisp_head = 0;
    g_wdisp_tail = 0;
}

#endif /* __wasm__ (browser window shell) */
