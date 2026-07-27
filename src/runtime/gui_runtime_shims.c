/* gui_runtime_shims.c -- the embedded WebView control's macOS-without-Cocoa
 * fallback.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * Embedded WebView (native browser control).
 *
 * A real, navigable web view is a heavyweight, per-platform native control
 * (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux). Only macOS
 * needs a backend here (gui_runtime_mac.m, ZAN_GUI_COCOA): a WKWebView subview
 * of the window's content view. Windows drives Edge WebView2 from Zan itself
 * (stdlib/Gui/WebView2.zan, straight against its COM interfaces) and every
 * other platform takes WebViewBackend's own fallback, so neither references
 * these exports. Only a macOS build that opts out of the Cocoa backend (the
 * SDL windowing shell) still binds them, and gets no-op stubs:
 * zan_gui_webview_create returns 0 so the Zan WebView widget detects the lack
 * of native support and paints an in-canvas placeholder instead of embedding
 * a live browser.
 * ======================================================================== */
#if defined(__APPLE__) && !defined(ZAN_GUI_COCOA)
/* profile_id selects a per-account isolation profile (see WebView.zan). When a
 * real backend is added here it should map profile_id to that engine's data
 * partitioning -- a WKWebsiteDataStore with a per-profile identifier. Until
 * then it is a no-op stub. */
EXPORT i64 zan_gui_webview_create(iptr hwnd, const char *profile_id) {
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
