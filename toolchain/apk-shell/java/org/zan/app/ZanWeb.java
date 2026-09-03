package org.zan.app;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.util.ArrayList;
import java.util.List;


/**
 * Embedded-browser bridge backing Gui.Component.WebView's WebViewBackend on
 * Android: one android.webkit.WebView (the system engine, updated through
 * Play) per Zan handle, overlaid on the SDL surface. The native side
 * (libzan_gui.so, gui_runtime_android.c) owns the handle table and state
 * cache; every view operation here hops to the UI thread, and page events
 * come back through the single {@link #zanGuiWebEvent} native sink.
 *
 * Threading contract: methods taking an Activity may be called from any
 * thread (they marshal internally); the zanGuiWebEvent JNI call always
 * arrives on a Java thread with a valid JNIEnv (UI thread, chrome thread or
 * the JavascriptInterface binder thread).
 */
public class ZanWeb {
    static {
        // The driver is already resident (DT_NEEDED of libmain.so); this
        // resolves the same file from nativeLibraryDir and is a no-op there.
        System.loadLibrary("zan_gui");
    }

    // ---- event kinds, mirrored 1:1 in gui_runtime_android.c -------------
    private static final int EV_PAGE_STARTED = 1;   // s1 = url
    private static final int EV_PAGE_FINISHED = 2;  // s1 = url
    private static final int EV_TITLE = 3;          // s2 = title
    private static final int EV_ERROR = 5;          // s1 = url, i1 = code
    private static final int EV_HISTORY = 6;        // s1 = url, i1 = canBack, i2 = canFwd
    private static final int EV_REQUEST = 7;        // s1 = url
    private static final int EV_MSG = 9;            // s1 = handler, s2 = body
    private static final int EV_EVAL = 10;          // s1 = json result

    private static final SparseArray<WebView> VIEWS = new SparseArray<WebView>();
    private static final SparseArray<List<String>> SCRIPTS_START = new SparseArray<List<String>>();
    private static final SparseArray<List<String>> SCRIPTS_END = new SparseArray<List<String>>();
    private static final SparseArray<List<String>> HANDLERS = new SparseArray<List<String>>();
    private static final Handler UI = new Handler(Looper.getMainLooper());

    private ZanWeb() {}

    private static native void zanGuiWebEvent(int id, int kind,
                                              String s1, String s2,
                                              int i1, int i2);

    private static ViewGroup content(Activity act) {
        return (ViewGroup) act.findViewById(android.R.id.content);
    }

    private static WebView view(int id) { return VIEWS.get(id); }

    // ---- lifecycle -------------------------------------------------------

    public static void create(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                if (view(id) != null) { return; }
                WebView wv = new WebView(act);
                WebSettings s = wv.getSettings();
                s.setJavaScriptEnabled(true);
                s.setDomStorageEnabled(true);
                s.setMediaPlaybackRequiresUserGesture(false);
                wv.setBackgroundColor(Color.TRANSPARENT);
                wv.setWebViewClient(new ZanClient(id));
                wv.setWebChromeClient(new ZanChrome(id));
                wv.setVisibility(WebView.GONE);
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(0, 0);
                content(act).addView(wv, lp);
                VIEWS.put(id, wv);
            }
        });
    }

    public static void destroy(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) { return; }
                ViewGroup parent = (ViewGroup) wv.getParent();
                if (parent != null) { parent.removeView(wv); }
                wv.destroy();
                VIEWS.remove(id);
                SCRIPTS_START.remove(id);
                SCRIPTS_END.remove(id);
                HANDLERS.remove(id);
            }
        });
    }

    // ---- geometry / visibility ------------------------------------------

    public static void frame(final Activity act, final int id,
                             final int x, final int y,
                             final int w, final int h) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) { return; }
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
                lp.setMargins(x, y, 0, 0);
                wv.setLayoutParams(lp);
            }
        });
    }

    public static void visible(final Activity act, final int id, final int v) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv != null) {
                    wv.setVisibility(v != 0 ? WebView.VISIBLE : WebView.GONE);
                }
            }
        });
    }

    /**
     * Partial-occlusion clip, "x,y,w,h;..." window-pixel rects ("" = fully
     * covered). The union's bounding box becomes the view's clip bounds in
     * view-local coordinates; an empty spec hides the view, matching the
     * macOS CAShapeLayer mask behavior.
     */
    public static void clip(final Activity act, final int id, final String spec) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) { return; }
                if (spec == null || spec.length() == 0) {
                    wv.setVisibility(WebView.GONE);
                    return;
                }
                ViewGroup parent = (ViewGroup) wv.getParent();
                int vx = 0, vy = 0;
                if (parent != null) {
                    vx = wv.getLeft();
                    vy = wv.getTop();
                }
                int minX = 0, minY = 0, maxX = 0, maxY = 0;
                int rects = 0;
                for (String part : spec.split(";")) {
                    String[] c = part.split(",");
                    if (c.length < 4) { continue; }
                    try {
                        int rx = Integer.parseInt(c[0].trim()) - vx;
                        int ry = Integer.parseInt(c[1].trim()) - vy;
                        int rw = Integer.parseInt(c[2].trim());
                        int rh = Integer.parseInt(c[3].trim());
                        if (rw <= 0 || rh <= 0) { continue; }
                        if (rects == 0) {
                            minX = rx; minY = ry; maxX = rx + rw; maxY = ry + rh;
                        } else {
                            minX = Math.min(minX, rx); minY = Math.min(minY, ry);
                            maxX = Math.max(maxX, rx + rw);
                            maxY = Math.max(maxY, ry + rh);
                        }
                        rects++;
                    } catch (NumberFormatException ignored) { }
                }
                if (rects == 0) {
                    wv.setVisibility(WebView.GONE);
                    return;
                }
                wv.setClipBounds(new Rect(minX, minY, maxX, maxY));
                wv.setVisibility(WebView.VISIBLE);
            }
        });
    }

    // ---- navigation ------------------------------------------------------

    public static void navigate(final Activity act, final int id, final String url) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv != null && url != null && url.length() > 0) {
                    wv.loadUrl(url);
                }
            }
        });
    }

    public static void loadHtml(final Activity act, final int id,
                                final String html, final String baseUrl) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv != null) {
                    wv.loadDataWithBaseURL(baseUrl, html, "text/html", "utf-8", null);
                }
            }
        });
    }

    public static void back(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() { public void run() {
            WebView wv = view(id); if (wv != null && wv.canGoBack()) { wv.goBack(); }
        }});
    }

    public static void forward(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() { public void run() {
            WebView wv = view(id); if (wv != null && wv.canGoForward()) { wv.goForward(); }
        }});
    }

    public static void reload(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() { public void run() {
            WebView wv = view(id); if (wv != null) { wv.reload(); }
        }});
    }

    public static void stop(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() { public void run() {
            WebView wv = view(id); if (wv != null) { wv.stopLoading(); }
        }});
    }

    /** Native waits (up to its own timeout) for the EV_EVAL this posts back. */
    public static void eval(final Activity act, final int id, final String js) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) {
                    zanGuiWebEvent(id, EV_EVAL, "", "", 0, 0);
                    return;
                }
                wv.evaluateJavascript(js, new android.webkit.ValueCallback<String>() {
                    public void onReceiveValue(String json) {
                        zanGuiWebEvent(id, EV_EVAL, json == null ? "" : json, "", 0, 0);
                    }
                });
            }
        });
    }

    /** Fire-and-forget variant: no result is reported back. */
    public static void evalAsync(final Activity act, final int id, final String js) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv != null) { wv.evaluateJavascript(js, null); }
            }
        });
    }

    // ---- user scripts / styles / message handlers ------------------------

    public static void addScript(final Activity act, final int id,
                                 final String js, final int atEnd) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                list(atEnd != 0 ? SCRIPTS_END : SCRIPTS_START, id).add(js);
            }
        });
    }

    public static void addStyle(final Activity act, final int id, final String css) {
        // Styles are user scripts that re-append a <style> element on every
        // page; org.json is public API on Android, quote() yields a safe JS
        // string literal.
        final String script = "(function(){var s=document.createElement('style');"
            + "s.textContent=" + org.json.JSONObject.quote(css == null ? "" : css)
            + ";document.head.appendChild(s);})();";
        act.runOnUiThread(new Runnable() {
            public void run() { list(SCRIPTS_END, id).add(script); }
        });
    }

    public static void removeScripts(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                SCRIPTS_START.remove(id);
                SCRIPTS_END.remove(id);
            }
        });
    }

    /**
     * Registers window.webkit.messageHandlers.<name>.postMessage(x) on every
     * page load: a JavascriptInterface named <name> plus a small shim that
     * reproduces the WKWebView calling convention the Zan side documents.
     */
    public static void addHandler(final Activity act, final int id, final String name) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) { return; }
                List<String> names = HANDLERS.get(id);
                if (names == null) {
                    names = new ArrayList<String>();
                    HANDLERS.put(id, names);
                }
                if (!names.contains(name)) { names.add(name); }
                wv.addJavascriptInterface(new MsgSink(id, name), name);
                injectShims(wv, names);
            }
        });
    }

    public static void removeHandler(final Activity act, final int id, final String name) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv == null) { return; }
                List<String> names = HANDLERS.get(id);
                if (names != null) { names.remove(name); }
                if (android.os.Build.VERSION.SDK_INT >= 28) {
                    wv.removeJavascriptInterface(name);
                }
            }
        });
    }

    private static void injectShims(WebView wv, List<String> names) {
        if (names.isEmpty()) { return; }
        StringBuilder sb = new StringBuilder("window.webkit=window.webkit||{};"
            + "window.webkit.messageHandlers=window.webkit.messageHandlers||{};");
        for (String n : names) {
            sb.append("window.webkit.messageHandlers['").append(n)
              .append("']={postMessage:function(x){window.").append(n)
              .append(".postMessage(x===undefined?null:(''+x));}};");
        }
        wv.evaluateJavascript(sb.toString(), null);
    }

    // ---- site data -------------------------------------------------------

    /** CookieManager is process-global and callable off the UI thread. */
    public static String getCookie(String url) {
        String c = CookieManager.getInstance().getCookie(url);
        return c == null ? "" : c;
    }

    public static void setCookie(String url, String name, String value) {
        CookieManager cm = CookieManager.getInstance();
        cm.setCookie(url, name + "=" + value);
        cm.flush();
    }

    public static void clearCookies() {
        CookieManager cm = CookieManager.getInstance();
        cm.removeAllCookies(null);
        cm.flush();
    }

    public static void clearData(final Activity act, final int id) {
        act.runOnUiThread(new Runnable() {
            public void run() {
                WebView wv = view(id);
                if (wv != null) {
                    wv.clearCache(true);
                    wv.clearHistory();
                    wv.clearFormData();
                }
                android.webkit.WebStorage.getInstance().deleteAllData();
            }
        });
        clearCookies();
    }

    // ---- clients ---------------------------------------------------------

    private static List<String> list(SparseArray<List<String>> map, int id) {
        List<String> l = map.get(id);
        if (l == null) {
            l = new ArrayList<String>();
            map.put(id, l);
        }
        return l;
    }

    private static final class ZanClient extends WebViewClient {
        private final int id;
        ZanClient(int id) { this.id = id; }

        @Override
        public void onPageStarted(WebView wv, String url, android.graphics.Bitmap favicon) {
            zanGuiWebEvent(id, EV_PAGE_STARTED, url == null ? "" : url, "", 0, 0);
            for (String js : list(SCRIPTS_START, id)) { wv.evaluateJavascript(js, null); }
            injectShims(wv, list(HANDLERS, id));
        }

        @Override
        public void onPageFinished(WebView wv, String url) {
            for (String js : list(SCRIPTS_END, id)) { wv.evaluateJavascript(js, null); }
            zanGuiWebEvent(id, EV_PAGE_FINISHED, url == null ? "" : url, "", 0, 0);
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView wv, WebResourceRequest req) {
            zanGuiWebEvent(id, EV_REQUEST, req.getUrl().toString(), "", 0, 0);
            return false;
        }

        @Override
        public void onReceivedError(WebView wv, WebResourceRequest req,
                                    WebResourceError err) {
            if (req.isForMainFrame()) {
                zanGuiWebEvent(id, EV_ERROR, req.getUrl().toString(),
                               err.getDescription() == null ? ""
                                                           : err.getDescription().toString(),
                               err.getErrorCode(), 0);
            }
        }
    }

    private static final class ZanChrome extends WebChromeClient {
        private final int id;
        ZanChrome(int id) { this.id = id; }

        @Override
        public void onReceivedTitle(WebView wv, String title) {
            zanGuiWebEvent(id, EV_TITLE, "", title == null ? "" : title, 0, 0);
        }

        @Override
        public void onProgressChanged(WebView wv, int progress) {
            zanGuiWebEvent(id, 4, "", "", progress, 0);
        }
    }

    /** JS → native sink bound under the handler name (WKWebView-shaped). */
    private static final class MsgSink {
        private final int id;
        private final String name;
        MsgSink(int id, String name) { this.id = id; this.name = name; }

        @JavascriptInterface
        public void postMessage(String body) {
            zanGuiWebEvent(id, EV_MSG, name, body == null ? "" : body, 0, 0);
        }
    }
}
