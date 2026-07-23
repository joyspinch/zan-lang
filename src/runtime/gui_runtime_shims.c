/* gui_runtime_shims.c -- macOS/other windowing dispatch, portable shims for Win32-only
 * functions and the embedded WebView control.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * macOS (and other non-Windows, non-Linux) windowing.
 *
 * When ZAN_GUI_COCOA is defined the real Cocoa backend in gui_runtime_mac.m
 * provides these; otherwise they are no-op stubs so the library still links.
 * Text rendering is the shared software path above, so it is omitted here.
 * The unified SDL backend (ZAN_GUI_SDL) supplies real implementations, so the
 * stubs are compiled out there too. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)

EXPORT i64 zan_gui_create_window(const char *t, i64 w, i64 h) { (void)t;(void)w;(void)h; return 0; }
EXPORT i64 zan_gui_show_window(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_wait_event(void) { return -1; }
EXPORT i64 zan_gui_wait_event_timeout(i64 ms) { (void)ms; return -1; }
EXPORT i64 zan_gui_poll_event(void) { return -1; }
EXPORT i64 zan_gui_wake(void) { return 0; }
EXPORT i64 zan_gui_event_kind(void) { return 0; }
EXPORT i64 zan_gui_event_x(void) { return 0; }
EXPORT i64 zan_gui_event_y(void) { return 0; }
EXPORT i64 zan_gui_event_button(void) { return 0; }
EXPORT i64 zan_gui_event_keycode(void) { return 0; }
EXPORT i64 zan_gui_event_mods(void) { return 0; }
EXPORT i64 zan_gui_window_width(void) { return 0; }
EXPORT i64 zan_gui_window_height(void) { return 0; }
EXPORT i64 zan_gui_event_hwnd(void) { return 0; }
EXPORT i64 zan_gui_client_width(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_client_height(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_present(i64 h, i64 s) { (void)h;(void)s; return 1; }
EXPORT i64 zan_gui_set_title(i64 h, const char *t) { (void)h;(void)t; return 0; }
EXPORT i64 zan_gui_set_cursor(i64 c) { (void)c; return 0; }
EXPORT i64 zan_gui_get_tick_ms(void) { return 0; }
EXPORT void zan_gui_sleep_ms(i64 ms) { (void)ms; }

#endif

/* ========================================================================
 * Portable / fallback shims for functions native only to Win32.
 * ========================================================================
 * The Win32, X11 and Cocoa backends all draw their own client-side title bar
 * and supply these; only the headless/no-op fallback reports 0. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_caption_button_width(void) { return 0; }
EXPORT i64 zan_gui_titlebar_height(void) { return 0; }
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) { (void)hwnd_val; (void)count; return 0; }
#endif

/* write_file is portable across every non-Win32 backend (X11, Cocoa and the
 * no-op fallback all need it), so it lives outside the CSD-metrics guard. The
 * SDL backend supplies its own (SDL_IOStream) write_file. */
#if !defined(_WIN32) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    if (utf8) fwrite(utf8, 1, strlen(utf8), f);
    fclose(f);
    return 0;
}
#endif

/* Native translucent glass. Windows has the DWM-acrylic implementation above.
 * macOS (NSVisualEffectView) and Linux (compositor blur) get real backends in
 * their own sections; any other target links a no-op so the cross-platform
 * Gui.Native.Window.EnableGlass entry points resolve everywhere. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1;
}
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i64 zan_gui_set_opacity(i64 hwnd_val, i64 percent) {
    (void)hwnd_val; (void)percent; return 1;
}
#endif

/* Window management: X11 has real implementations in the __linux__ branch
 * above; the Cocoa backend (ZAN_GUI_COCOA) provides them on macOS. Any other
 * non-Windows target falls back to no-ops. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_get_dpi_scale(void) { return 100; }
EXPORT i64 zan_gui_close_window(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_destroy_window(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_minimize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_window_visible(i64 hwnd_val) { (void)hwnd_val; return 1; }
EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) { (void)hwnd_val; (void)on; return 0; }
EXPORT i64 zan_gui_set_clipboard(const char *utf8) { (void)utf8; return 0; }
EXPORT const char *zan_gui_get_clipboard(void) { return ""; }
EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) { (void)x; (void)y; }
#endif

/* ========================================================================
 * Embedded WebView (native browser control).
 *
 * A real, navigable web view is a heavyweight, per-platform native control
 * (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux). Only the
 * macOS backend (gui_runtime_mac.m, ZAN_GUI_COCOA) currently implements it, as
 * a WKWebView subview of the window's content view with cookie access, a
 * navigation delegate (URL/title/status monitoring) and JavaScript eval.
 * Everywhere else these are no-op stubs: zan_gui_webview_create returns 0 so
 * the Zan WebView widget can detect the lack of native support and paint an
 * in-canvas placeholder instead of embedding a live browser.
 * ======================================================================== */
#if !defined(ZAN_GUI_COCOA)
/* profile_id selects a per-account isolation profile (see WebView.zan). When a
 * real backend is added here it should map profile_id to that engine's data
 * partitioning: on Windows a WebView2 per-profile user-data folder (distinct
 * CreateCoreWebView2EnvironmentWithOptions userDataFolder, or ICoreWebView2Profile
 * on newer runtimes); on Linux a WebKitGTK WebKitWebsiteDataManager with a
 * per-profile base data/cache directory. Until then it is a no-op stub. */
EXPORT i64 zan_gui_webview_create(i64 hwnd, const char *profile_id) {
    (void)hwnd; (void)profile_id; return 0;
}
EXPORT void zan_gui_webview_destroy(i64 h) { (void)h; }
EXPORT void zan_gui_webview_set_frame(i64 h, i64 x, i64 y, i64 w, i64 hh) {
    (void)h; (void)x; (void)y; (void)w; (void)hh;
}
EXPORT void zan_gui_webview_set_visible(i64 h, i64 visible) {
    (void)h; (void)visible;
}
EXPORT void zan_gui_webview_navigate(i64 h, const char *url) {
    (void)h; (void)url;
}
EXPORT void zan_gui_webview_load_html(i64 h, const char *html, const char *base_url) {
    (void)h; (void)html; (void)base_url;
}
EXPORT void zan_gui_webview_back(i64 h) { (void)h; }
EXPORT void zan_gui_webview_forward(i64 h) { (void)h; }
EXPORT void zan_gui_webview_reload(i64 h) { (void)h; }
EXPORT void zan_gui_webview_stop(i64 h) { (void)h; }
EXPORT i64 zan_gui_webview_can_go_back(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_can_go_forward(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_is_loading(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_nav_seq(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_last_status(i64 h) { (void)h; return 0; }
EXPORT const char *zan_gui_webview_get_url(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_get_title(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_last_request(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_eval(i64 h, const char *js) {
    (void)h; (void)js; return "";
}
EXPORT const char *zan_gui_webview_get_cookies(i64 h, const char *url) {
    (void)h; (void)url; return "";
}
EXPORT void zan_gui_webview_set_cookie(i64 h, const char *url,
                                       const char *name, const char *value) {
    (void)h; (void)url; (void)name; (void)value;
}
EXPORT void zan_gui_webview_clear_cookies(i64 h) { (void)h; }
#endif
