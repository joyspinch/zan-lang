/* gui_runtime_wasm */

#ifdef __wasm__

#include <string.h>
#include <time.h>

#include "../common/zan_abi.h"

/* 内部辅助逻辑 */
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

/* 核心系统底层抽象与内存语义契约 */

typedef struct {
    int w, h;      /* 底层系统交互与数据协议契约 */
    int shown;
    int closed;    /* 核心系统底层抽象与内存语义契约 */
} zan_wasm_win_t;
static zan_wasm_win_t g_wwin;
#define ZAN_WASM_HWND ((i64)(intptr_t)&g_wwin)

static int  g_window_width  = 0;
static int  g_window_height = 0;

/* 核心系统底层抽象与内存语义契约 */

typedef struct { int e[8]; i64 win; } zan_wev_t;
#define ZAN_WQ_CAP 512
static zan_wev_t g_wq[ZAN_WQ_CAP];
static int g_wq_head = 0, g_wq_tail = 0;

static int g_pending_event[8];
static i64 g_event_win = 0;
static long long g_ev_seq = 0;

/* 内部辅助逻辑 */
static void wq_push(int kind, int x, int y, int button, int code, int mods,
                    int flag) {
    if (kind == 7) { /* 底层系统交互与数据协议契约 */
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

/* 底层系统交互与数据协议契约 */
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

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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
        /* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
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
    /* 底层系统交互与数据协议契约 */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    zan__env_present((i32)(intptr_t)s->pixels, s->width, s->height);
    g_dirty_count_wasm = 0;
    g_dirty_overflow_wasm = 0;
    return 0;
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
EXPORT i32 zan_gui_get_dpi_scale(void) { return 100; }

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
EXPORT const char *zan_gui_android_files_dir(void) { return "."; }

/* 内部辅助逻辑 */
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

/* UI dispatch (Gui */
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
    /* zan_abi */
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
    if (next == g_wdisp_head) return 0; /* 核心系统底层抽象与内存语义契约 */
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

#endif /* 核心系统底层抽象与内存语义契约 */
