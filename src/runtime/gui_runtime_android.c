/* gui_runtime_android.c -- Android system-WebView backend for the embedded
 * browser control (Gui.Component.WebView's WebViewBackend).
 *
 * #included by gui_runtime.c after gui_runtime_sdl.c; compiles only in
 * __ANDROID__ builds of the zan_gui driver. The engine is the per-device
 * system WebView: one android.webkit.WebView per Zan handle, overlaid on the
 * SDL surface by the Java side (org.zan.app.ZanWeb, packaged in the APK
 * shell's classes.dex -- see toolchain/apk-shell/java). Every view operation
 * hops to the UI thread inside ZanWeb; page events come back through the
 * single zanGuiWebEvent JNI sink and update the per-handle state cache that
 * the zan_gui_webview_* exports report.
 *
 * Limitations vs the macOS WKWebView backend: cookies are process-global
 * (CookieManager has no per-profile stores), so profileId is accepted but
 * ignored; eval marshals to the UI thread and waits up to 3 s for the
 * result (the JSON encoding evaluateJavascript produces, not WKWebView's
 * description format). */

#if defined(__ANDROID__)

#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Diagnostics go to logcat under "zan_awv"; the bridge has no other console. */
#include <android/log.h>
#define AWV_LOG(...) \
    __android_log_print(ANDROID_LOG_INFO, "zan_awv", __VA_ARGS__)

#ifndef ZAN_ANDROID_WV_MAX
#define ZAN_ANDROID_WV_MAX 32
#endif
#define ZAN_AWV_MSG_MAX 128     /* queued "<handler>\t<body>" entries */
#define ZAN_AWV_EVAL_TIMEOUT 3  /* seconds, matching the macOS spin */

/* Event kinds sent by org.zan.app.ZanWeb.zanGuiWebEvent -- keep in step
 * with the EV_* constants in ZanWeb.java. */
#define ZAN_AWV_EV_PAGE_STARTED 1
#define ZAN_AWV_EV_PAGE_FINISHED 2
#define ZAN_AWV_EV_TITLE 3
#define ZAN_AWV_EV_ERROR 5
#define ZAN_AWV_EV_HISTORY 6
#define ZAN_AWV_EV_REQUEST 7
#define ZAN_AWV_EV_MSG 9
#define ZAN_AWV_EV_EVAL 10

typedef struct {
    int used;
    /* per-handle string buffers returned to Zan (strdup-replaced, owned) */
    char *urlBuf, *titleBuf, *reqBuf, *evalBuf, *takeBuf, *cookieBuf, *clipBuf;
    int canBack, canFwd, loading, navSeq, status;
    int frameX, frameY, frameW, frameH;
    /* JS -> Zan message ring */
    char *msgs[ZAN_AWV_MSG_MAX];
    int msgHead, msgCount, msgDropped;
    /* eval synchronization: UI thread bumps evalSeq, waiter waits for it */
    unsigned evalSeq;
} zaw_t;

static zaw_t g_awv[ZAN_ANDROID_WV_MAX];
static pthread_mutex_t g_awv_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_awv_cond = PTHREAD_COND_INITIALIZER;

static JavaVM *g_awv_vm;
static jclass g_awv_bridge;
static struct {
    jmethodID create, destroy, frame, visible, navigate, loadHtml;
    jmethodID back, forward, reload, stop, eval, evalAsync;
    jmethodID addHandler, removeHandler, addScript, addStyle, removeScripts;
    jmethodID clip, clearData, getCookie, setCookie, clearCookies;
} g_awv_mid;

static char *awv_store(char **buf, const char *s) {
    char *nb = strdup(s ? s : "");
    if (!nb) { return *buf ? *buf : (char *)""; }
    free(*buf);
    *buf = nb;
    return nb;
}

static zaw_t *awv_slot(int h) {
    if (h < 1 || h > ZAN_ANDROID_WV_MAX) { return NULL; }
    zaw_t *w = &g_awv[h - 1];
    return w->used ? w : NULL;
}

/* ART's NewStringUTF is Modified-UTF-8; real page URLs/HTML carry 4-byte
 * sequences, so take the byte-array path whenever one is present. */
static jstring awv_jstr(JNIEnv *env, const char *utf8) {
    if (!utf8) { utf8 = ""; }
    int needsBytes = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        if ((*p & 0xF8) == 0xF0) { needsBytes = 1; break; }
    }
    if (!needsBytes) {
        jstring s = (*env)->NewStringUTF(env, utf8);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
        return s;
    }
    size_t n = strlen(utf8);
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)n);
    if (!arr) { (*env)->ExceptionClear(env); return NULL; }
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)utf8);
    static jclass strCls;
    static jmethodID strCtor;
    static jstring utf8Name;
    if (!strCtor) {
        jclass c = (*env)->FindClass(env, "java/lang/String");
        if (!c) { (*env)->ExceptionClear(env); return NULL; }
        strCls = (jclass)(*env)->NewGlobalRef(env, c);
        (*env)->DeleteLocalRef(env, c);
        strCtor = (*env)->GetMethodID(env, strCls, "<init>",
                                      "([BLjava/lang/String;)V");
        if (!strCtor) { (*env)->ExceptionClear(env); return NULL; }
        jstring s = (*env)->NewStringUTF(env, "UTF-8");
        utf8Name = (jstring)(*env)->NewGlobalRef(env, s);
        (*env)->DeleteLocalRef(env, s);
    }
    jstring s = (jstring)(*env)->NewObject(env, strCls, strCtor, arr, utf8Name);
    (*env)->DeleteLocalRef(env, arr);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    return s;
}

static char *awv_cstr(JNIEnv *env, jstring s) {
    if (!s) { return strdup(""); }
    const char *p = (*env)->GetStringUTFChars(env, s, NULL);
    char *out = strdup(p ? p : "");
    if (p) { (*env)->ReleaseStringUTFChars(env, s, p); }
    return out;
}

/* Cache the bridge class and entry points. Runs on the first webview call:
 * the app classloader is reached through the activity because FindClass from
 * an SDL thread only sees the system loader. */
static int awv_init(void) {
    static int done = -1;
    if (done == 0) { return 0; }
    if (done > 0) { return -1; }
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) { AWV_LOG("init: no JNIEnv"); return -1; }
    if ((*env)->GetJavaVM(env, &g_awv_vm) != JNI_OK) {
        AWV_LOG("init: GetJavaVM failed");
        return -1;
    }
    jobject act = SDL_GetAndroidActivity();
    if (!act) { AWV_LOG("init: no activity"); return -1; }

    jclass aclazz = (*env)->GetObjectClass(env, act);
    jmethodID gcl = (*env)->GetMethodID(env, aclazz, "getClassLoader",
                                        "()Ljava/lang/ClassLoader;");
    (*env)->DeleteLocalRef(env, aclazz);
    if (!gcl) { AWV_LOG("init: no getClassLoader"); goto fail; }
    jobject loader = (*env)->CallObjectMethod(env, act, gcl);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        AWV_LOG("init: getClassLoader threw");
        goto fail;
    }
    jclass lclazz = (*env)->GetObjectClass(env, loader);
    jmethodID load = (*env)->GetMethodID(env, lclazz, "loadClass",
                                         "(Ljava/lang/String;)Ljava/lang/Class;");
    (*env)->DeleteLocalRef(env, lclazz);
    if (!load) {
        (*env)->DeleteLocalRef(env, loader);
        AWV_LOG("init: no loadClass");
        goto fail;
    }
    jstring name = (*env)->NewStringUTF(env, "org.zan.app.ZanWeb");
    jclass bridge = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
    /* loader stays alive until after the call: deleting the local ref early
     * leaves the receiver dangling (segfault inside ART arg marshaling). */
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, name);
    if ((*env)->ExceptionCheck(env) || !bridge) {
        jthrowable ex = (*env)->ExceptionOccurred(env);
        if (ex) {
            jclass ez = (*env)->GetObjectClass(env, ex);
            jmethodID gm = (*env)->GetMethodID(env, ez, "toString",
                                               "()Ljava/lang/String;");
            jstring js = (jstring)(*env)->CallObjectMethod(env, ex, gm);
            const char *p = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
            AWV_LOG("init: loadClass threw: %s", p ? p : "?");
            if (p) { (*env)->ReleaseStringUTFChars(env, js, p); }
            (*env)->ExceptionClear(env);
        } else {
            AWV_LOG("init: loadClass returned null");
        }
        goto fail;
    }
    g_awv_bridge = (jclass)(*env)->NewGlobalRef(env, bridge);
    (*env)->DeleteLocalRef(env, bridge);

    typedef struct { jmethodID *slot; const char *name; const char *sig; } entry;
    entry table[] = {
        { &g_awv_mid.create, "create", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.destroy, "destroy", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.frame, "frame", "(Landroid/app/Activity;IIIII)V" },
        { &g_awv_mid.visible, "visible", "(Landroid/app/Activity;II)V" },
        { &g_awv_mid.navigate, "navigate",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.loadHtml, "loadHtml",
          "(Landroid/app/Activity;ILjava/lang/String;Ljava/lang/String;)V" },
        { &g_awv_mid.back, "back", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.forward, "forward", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.reload, "reload", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.stop, "stop", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.eval, "eval",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.evalAsync, "evalAsync",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.addHandler, "addHandler",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.removeHandler, "removeHandler",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.addScript, "addScript",
          "(Landroid/app/Activity;ILjava/lang/String;I)V" },
        { &g_awv_mid.addStyle, "addStyle",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.removeScripts, "removeScripts", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.clip, "clip",
          "(Landroid/app/Activity;ILjava/lang/String;)V" },
        { &g_awv_mid.clearData, "clearData", "(Landroid/app/Activity;I)V" },
        { &g_awv_mid.getCookie, "getCookie",
          "(Ljava/lang/String;)Ljava/lang/String;" },
        { &g_awv_mid.setCookie, "setCookie",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V" },
        { &g_awv_mid.clearCookies, "clearCookies", "()V" },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        *table[i].slot = (*env)->GetStaticMethodID(env, g_awv_bridge,
                                                   table[i].name, table[i].sig);
        if (!*table[i].slot) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteGlobalRef(env, g_awv_bridge);
            g_awv_bridge = NULL;
            AWV_LOG("init: missing ZanWeb method %s %s",
                    table[i].name, table[i].sig);
            goto fail;
        }
    }
    done = 0;
    AWV_LOG("init: ok");
    return 0;
fail:
    done = 1;
    AWV_LOG("init: failed");
    return -1;
}

/* JNIEnv for the calling thread: SDL threads arrive attached, UI/Javascript
 * threads come pre-attached with their own env through the JNI sink. */
static JNIEnv *awv_env(void) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (env) { return env; }
    if (!g_awv_vm) { return NULL; }
    if ((*g_awv_vm)->AttachCurrentThread(g_awv_vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    return env;
}

/* ZanWeb's static methods all start with the Activity (fetched from SDL);
 * these helpers dispatch each argument shape and clear any Java exception.
 * String arguments accept NULL. */
static int awv_begin(JNIEnv **env, jobject *act) {
    if (!g_awv_bridge) { return -1; }
    *env = awv_env();
    if (!*env) { return -1; }
    *act = SDL_GetAndroidActivity();
    if (!*act) { return -1; }
    return 0;
}

static void awv_end(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
}

static void awv_call_i(jmethodID mid, i32 a) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a);
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

static void awv_call_ii(jmethodID mid, i32 a, i32 b) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a, (jint)b);
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

static void awv_call_iiiii(jmethodID mid, i32 a, i32 b, i32 c, i32 d, i32 e) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a,
                                 (jint)b, (jint)c, (jint)d, (jint)e);
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

static void awv_call_s(jmethodID mid, i32 a, const char *s) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    jstring js = awv_jstr(env, s);
    if (js) {
        (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a, js);
        (*env)->DeleteLocalRef(env, js);
    }
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

static void awv_call_si(jmethodID mid, i32 a, const char *s, i32 b) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    jstring js = awv_jstr(env, s);
    if (js) {
        (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a,
                                     js, (jint)b);
        (*env)->DeleteLocalRef(env, js);
    }
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

static void awv_call_ss(jmethodID mid, i32 a, const char *s1, const char *s2) {
    JNIEnv *env; jobject act;
    if (awv_begin(&env, &act) != 0) { return; }
    jstring j1 = awv_jstr(env, s1);
    jstring j2 = j1 ? awv_jstr(env, s2) : NULL;
    if (j1 && j2) {
        (*env)->CallStaticVoidMethod(env, g_awv_bridge, mid, act, (jint)a,
                                     j1, j2);
    }
    if (j1) { (*env)->DeleteLocalRef(env, j1); }
    if (j2) { (*env)->DeleteLocalRef(env, j2); }
    awv_end(env);
    (*env)->DeleteLocalRef(env, act);
}

/* The single event sink. Called on Java threads; updates the state cache. */
EXPORT void JNICALL
Java_org_zan_app_ZanWeb_zanGuiWebEvent(JNIEnv *env, jclass clazz, jint id,
                                       jint kind, jstring s1, jstring s2,
                                       jint i1, jint i2) {
    (void)clazz; (void)i2;
    if (!g_awv_vm) {
        (*env)->GetJavaVM(env, &g_awv_vm);
    }
    char *a = awv_cstr(env, s1);
    char *b = awv_cstr(env, s2);
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot((int)id);
    if (w) {
        switch (kind) {
        case ZAN_AWV_EV_PAGE_STARTED:
            awv_store(&w->urlBuf, a);
            w->loading = 1;
            w->navSeq++;
            break;
        case ZAN_AWV_EV_PAGE_FINISHED:
            awv_store(&w->urlBuf, a);
            w->loading = 0;
            w->status = 200;
            w->navSeq++;
            break;
        case ZAN_AWV_EV_TITLE:
            awv_store(&w->titleBuf, b);
            break;
        case ZAN_AWV_EV_ERROR:
            awv_store(&w->urlBuf, a);
            w->loading = 0;
            w->status = (int)i1;
            w->navSeq++;
            break;
        case ZAN_AWV_EV_HISTORY:
            awv_store(&w->urlBuf, a);
            w->canBack = (int)i1;
            w->canFwd = (int)i2;
            break;
        case ZAN_AWV_EV_REQUEST:
            awv_store(&w->reqBuf, a);
            break;
    case ZAN_AWV_EV_MSG: {
        size_t need = strlen(a) + strlen(b) + 2;
            char *entry = malloc(need);
            if (entry) {
                snprintf(entry, need, "%s\t%s", a, b);
                if (w->msgCount >= ZAN_AWV_MSG_MAX) {
                    free(w->msgs[w->msgHead]);
                    w->msgs[w->msgHead] = entry;
                    w->msgHead = (w->msgHead + 1) % ZAN_AWV_MSG_MAX;
                    w->msgDropped++;
                } else {
                    int tail = (w->msgHead + w->msgCount) % ZAN_AWV_MSG_MAX;
                    w->msgs[tail] = entry;
                    w->msgCount++;
                }
            }
            break;
        }
        case ZAN_AWV_EV_EVAL:
            awv_store(&w->evalBuf, a);
            w->evalSeq++;
            pthread_cond_broadcast(&g_awv_cond);
            break;
        default:
            break;
        }
    }
    pthread_mutex_unlock(&g_awv_lock);
    free(a);
    free(b);
}

/* ---- zan_gui_webview_* exports (see WebViewBackend.zan for the contract) */

EXPORT i32 zan_gui_webview_create(i64 hwnd, const char *profileId) {
    (void)hwnd;      /* one SDL window == the whole activity surface */
    (void)profileId; /* CookieManager is process-global: no per-profile stores */
    if (awv_init() != 0) { AWV_LOG("create: init failed"); return 0; }
    pthread_mutex_lock(&g_awv_lock);
    int h = 0;
    for (int i = 0; i < ZAN_ANDROID_WV_MAX; i++) {
        if (!g_awv[i].used) {
            memset(&g_awv[i], 0, sizeof(g_awv[i]));
            g_awv[i].used = 1;
            h = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_awv_lock);
    if (!h) { AWV_LOG("create: no free slot"); return 0; }
    AWV_LOG("create: handle %d", h);
    awv_call_i(g_awv_mid.create, h);
    return h;
}

EXPORT void zan_gui_webview_destroy(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.destroy, h);
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    if (w) {
        char **bufs[] = { &w->urlBuf, &w->titleBuf, &w->reqBuf, &w->evalBuf,
                          &w->takeBuf, &w->cookieBuf, &w->clipBuf };
        for (size_t i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++) {
            free(*bufs[i]);
        }
        for (int i = 0; i < w->msgCount; i++) {
            int idx = (w->msgHead + i) % ZAN_AWV_MSG_MAX;
            free(w->msgs[idx]);
        }
        memset(w, 0, sizeof(*w));
    }
    pthread_mutex_unlock(&g_awv_lock);
}

EXPORT void zan_gui_webview_set_frame(i32 h, i32 x, i32 y, i32 w, i32 hh) {
    zaw_t *slot = awv_slot(h);
    if (!slot) { return; }
    slot->frameX = x; slot->frameY = y;
    slot->frameW = w; slot->frameH = hh;
    awv_call_iiiii(g_awv_mid.frame, h, x, y, w, hh);
}

EXPORT void zan_gui_webview_set_visible(i32 h, i32 visible) {
    if (!awv_slot(h)) { return; }
    awv_call_ii(g_awv_mid.visible, h, visible);
}

EXPORT void zan_gui_webview_navigate(i32 h, const char *url) {
    if (!awv_slot(h) || !url || !*url) { return; }
    awv_call_s(g_awv_mid.navigate, h, url);
}

EXPORT void zan_gui_webview_load_html(i32 h, const char *html,
                                      const char *baseUrl) {
    if (!awv_slot(h) || !html) { return; }
    awv_call_ss(g_awv_mid.loadHtml, h, html, baseUrl ? baseUrl : "");
}

EXPORT void zan_gui_webview_back(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.back, h);
}

EXPORT void zan_gui_webview_forward(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.forward, h);
}

EXPORT void zan_gui_webview_reload(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.reload, h);
}

EXPORT void zan_gui_webview_stop(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.stop, h);
}

EXPORT i32 zan_gui_webview_can_go_back(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->canBack : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_can_go_forward(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->canFwd : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_is_loading(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->loading : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_nav_seq(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->navSeq : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_last_status(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->status : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT const char *zan_gui_webview_get_url(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    const char *v = (w && w->urlBuf) ? w->urlBuf : "";
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT const char *zan_gui_webview_get_title(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    const char *v = (w && w->titleBuf) ? w->titleBuf : "";
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT const char *zan_gui_webview_last_request(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    const char *v = (w && w->reqBuf) ? w->reqBuf : "";
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT const char *zan_gui_webview_eval(i32 h, const char *js) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    unsigned before = w ? w->evalSeq : 0;
    pthread_mutex_unlock(&g_awv_lock);
    if (!w) { return ""; }
    awv_call_s(g_awv_mid.eval, h, js ? js : "");
    /* UI thread reports through zanGuiWebEvent; bounded wait, macOS spins
     * its runloop for the same 3 s budget. */
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += ZAN_AWV_EVAL_TIMEOUT;
    pthread_mutex_lock(&g_awv_lock);
    while (w->evalSeq == before) {
        int rc = pthread_cond_timedwait(&g_awv_cond, &g_awv_lock, &dl);
        if (rc == ETIMEDOUT) { break; }
    }
    const char *v = (w->evalBuf && w->evalBuf[0]) ? w->evalBuf : "";
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT const char *zan_gui_webview_get_cookies(i32 h, const char *url) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    pthread_mutex_unlock(&g_awv_lock);
    if (!w) { return ""; }
    JNIEnv *env = awv_env();
    if (!env || !g_awv_bridge) { return ""; }
    jstring ju = awv_jstr(env, url ? url : "");
    if (!ju) { return ""; }
    jstring r = (*env)->CallStaticObjectMethod(env, g_awv_bridge,
                                               g_awv_mid.getCookie, ju);
    (*env)->DeleteLocalRef(env, ju);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return "";
    }
    char *c = awv_cstr(env, r);
    if (r) { (*env)->DeleteLocalRef(env, r); }
    pthread_mutex_lock(&g_awv_lock);
    const char *v = awv_store(&w->cookieBuf, c);
    pthread_mutex_unlock(&g_awv_lock);
    free(c);
    return v;
}

EXPORT void zan_gui_webview_set_cookie(i32 h, const char *url,
                                       const char *cookieName,
                                       const char *cookieValue) {
    if (!awv_slot(h) || !url || !cookieName) { return; }
    JNIEnv *env = awv_env();
    if (!env || !g_awv_bridge) { return; }
    jstring ju = awv_jstr(env, url);
    jstring jn = awv_jstr(env, cookieName);
    jstring jv = awv_jstr(env, cookieValue ? cookieValue : "");
    if (ju && jn && jv) {
        (*env)->CallStaticVoidMethod(env, g_awv_bridge, g_awv_mid.setCookie,
                                     ju, jn, jv);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
    if (ju) { (*env)->DeleteLocalRef(env, ju); }
    if (jn) { (*env)->DeleteLocalRef(env, jn); }
    if (jv) { (*env)->DeleteLocalRef(env, jv); }
}

EXPORT void zan_gui_webview_clear_cookies(i32 h) {
    JNIEnv *env = awv_env();
    if (!env || !g_awv_bridge) { return; }
    (*env)->CallStaticVoidMethod(env, g_awv_bridge, g_awv_mid.clearCookies);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
}

/* ---- optional bridge layer (dlopen-probed by WebViewBackend.zan) */

EXPORT i32 zan_gui_webview_add_handler(i32 h, const char *name) {
    if (!awv_slot(h) || !name || !*name) { return 0; }
    awv_call_s(g_awv_mid.addHandler, h, name);
    return 1;
}

EXPORT void zan_gui_webview_remove_handler(i32 h, const char *name) {
    if (!awv_slot(h)) { return; }
    awv_call_s(g_awv_mid.removeHandler, h, name ? name : "");
}

EXPORT const char *zan_gui_webview_take_message(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    if (!w || w->msgCount == 0) {
        pthread_mutex_unlock(&g_awv_lock);
        return "";
    }
    char *entry = w->msgs[w->msgHead];
    w->msgs[w->msgHead] = NULL;
    w->msgHead = (w->msgHead + 1) % ZAN_AWV_MSG_MAX;
    w->msgCount--;
    const char *v = awv_store(&w->takeBuf, entry);
    free(entry);
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_msg_pending(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->msgCount : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_msg_dropped(i32 h) {
    pthread_mutex_lock(&g_awv_lock);
    zaw_t *w = awv_slot(h);
    i32 v = w ? w->msgDropped : 0;
    pthread_mutex_unlock(&g_awv_lock);
    return v;
}

EXPORT i32 zan_gui_webview_add_script(i32 h, const char *js, i32 atEnd) {
    if (!awv_slot(h) || !js || !*js) { return 0; }
    awv_call_si(g_awv_mid.addScript, h, js, atEnd);
    return 1;
}

EXPORT i32 zan_gui_webview_add_style(i32 h, const char *css) {
    if (!awv_slot(h) || !css) { return 0; }
    awv_call_s(g_awv_mid.addStyle, h, css);
    return 1;
}

EXPORT void zan_gui_webview_remove_scripts(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.removeScripts, h);
}

EXPORT void zan_gui_webview_eval_async(i32 h, const char *js) {
    if (!awv_slot(h)) { return; }
    awv_call_s(g_awv_mid.evalAsync, h, js ? js : "");
}

EXPORT void zan_gui_webview_clear_data(i32 h) {
    if (!awv_slot(h)) { return; }
    awv_call_i(g_awv_mid.clearData, h);
}

/* Partial-occlusion clip, same contract as the macOS backend: "" = fully
 * covered (hide), otherwise clip the view to the union's bounding box.
 * Deduplicating identical specs matters -- this fires every frame. */
EXPORT void zan_gui_webview_set_clip(i32 h, const char *spec) {
    zaw_t *slot = awv_slot(h);
    if (!slot) { return; }
    const char *s = spec ? spec : "";
    if (slot->clipBuf && strcmp(slot->clipBuf, s) == 0) { return; }
    awv_store(&slot->clipBuf, s);
    awv_call_s(g_awv_mid.clip, h, s);
}

#endif /* __ANDROID__ */
