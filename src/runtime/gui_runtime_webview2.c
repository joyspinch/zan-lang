/* gui_runtime_webview2.c -- Windows embedded WebView (Edge WebView2) backend.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in a
 * fixed order; not compiled standalone.
 *
 * Compiled when ZAN_GUI_WEBVIEW2 is defined (CMake turns it on when the
 * vendored SDK header stdlib/Gui/native/webview2/WebView2.h is present). The
 * COM interfaces come from that MIDL-generated header, so the vtables are the
 * SDK's, not hand-transcribed. The loader entry point is resolved at run time
 * from WebView2Loader.dll (shipped as part of the zan_gui driver bundle) so a
 * GUI exe still starts on a machine without the WebView2 runtime -- the loader
 * or the environment creation simply fails, create returns 0, and the Zan
 * WebView widget falls back to its in-canvas placeholder.
 *
 * Creation and script evaluation are asynchronous in WebView2 but synchronous
 * in the Zan API, so both run a bounded nested message pump: the calls happen
 * on the UI thread, which is the thread WebView2 delivers completions on.
 */

#include <WebView2.h>
#include <wctype.h>

#define WV_MAX 8
#define WV_STR 2048

typedef struct {
    int used;
    HWND hwnd;
    ICoreWebView2Environment *env;
    ICoreWebView2Controller *ctrl;
    ICoreWebView2 *core;
    EventRegistrationToken nav_tok;
    EventRegistrationToken start_tok;
    EventRegistrationToken src_tok;
    int nav_seq;
    int last_status;
    int loading;
    char url[WV_STR];
    char title[WV_STR];
    char last_req[WV_STR];
    char *eval_out;
    char *cookie_out;
} zan_wv;

static zan_wv g_wv[WV_MAX];
static HMODULE g_wv_loader;
static int g_wv_com_init;

typedef HRESULT (STDMETHODCALLTYPE *zan_wv_create_env_fn)(
    PCWSTR browser_folder, PCWSTR user_data_folder,
    ICoreWebView2EnvironmentOptions *options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

static zan_wv_create_env_fn g_wv_create_env;

/* ---------------- small helpers ---------------- */

static wchar_t *zan_wv_wide(const char *utf8) {
    if (!utf8) utf8 = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    return w;
}

/* Copies a wide string into `dst` as UTF-8 (truncating), always NUL-ending. */
static void zan_wv_narrow_into(LPCWSTR w, char *dst, int cap) {
    dst[0] = '\0';
    if (!w) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, dst, cap, NULL, NULL);
    if (n <= 0) { dst[0] = '\0'; return; }
    dst[cap - 1] = '\0';
}

/* Heap UTF-8 copy of a wide string, for the unbounded results (eval, cookies). */
static char *zan_wv_narrow_dup(LPCWSTR w) {
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static zan_wv *zan_wv_get(i64 h) {
    if (h <= 0 || h > WV_MAX) return NULL;
    zan_wv *w = &g_wv[h - 1];
    if (!w->used) return NULL;
    return w;
}

/* Runs the thread's message queue until `*flag` is set or the budget expires.
 * WebView2 completions arrive as posted messages / APCs on the UI thread, so a
 * nested pump is how a synchronous wrapper waits for one. */
static int zan_wv_pump(volatile int *flag, int timeout_ms) {
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeout_ms;
    while (!*flag) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (*flag) return 1;
        }
        if (*flag) return 1;
        if (GetTickCount64() > deadline) return 0;
        MsgWaitForMultipleObjectsEx(0, NULL, 15, QS_ALLINPUT,
                                    MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
    }
    return 1;
}

/* ---------------- COM callback objects ----------------
 * Each handler is a plain struct whose first member is the SDK vtable pointer.
 * They live for the duration of one call (creation) or one event subscription,
 * are never shared across threads and are only ever released by WebView2, so a
 * simple refcount is enough. QueryInterface hands back `this` for any riid:
 * WebView2 only ever asks for the interface it was handed. */

#define ZAN_WV_UNKNOWN_IMPL(TYPE)                                             \
    static HRESULT STDMETHODCALLTYPE TYPE##_QI(TYPE *self, REFIID riid,       \
                                               void **out) {                  \
        (void)riid;                                                           \
        if (!out) return E_POINTER;                                           \
        *out = self;                                                          \
        self->refs++;                                                         \
        return S_OK;                                                          \
    }                                                                         \
    static ULONG STDMETHODCALLTYPE TYPE##_AddRef(TYPE *self) {                \
        return (ULONG)++self->refs;                                           \
    }                                                                         \
    static ULONG STDMETHODCALLTYPE TYPE##_Release(TYPE *self) {               \
        long r = --self->refs;                                                \
        if (r <= 0) { free(self); return 0; }                                 \
        return (ULONG)r;                                                      \
    }

/* -- environment created -- */
typedef struct {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl *vtbl;
    long refs;
    zan_wv *inst;
    volatile int done;
    HRESULT hr;
} zan_wv_env_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_env_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_env_cb_Invoke(
        zan_wv_env_cb *self, HRESULT hr, ICoreWebView2Environment *env) {
    self->hr = hr;
    if (SUCCEEDED(hr) && env) {
        self->inst->env = env;
        env->lpVtbl->AddRef(env);
    }
    self->done = 1;
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl
    g_wv_env_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *, REFIID,
        void **))zan_wv_env_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *))
        zan_wv_env_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *))
        zan_wv_env_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *, HRESULT,
        ICoreWebView2Environment *))zan_wv_env_cb_Invoke
};

/* -- controller created -- */
typedef struct {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl *vtbl;
    long refs;
    zan_wv *inst;
    volatile int done;
    HRESULT hr;
} zan_wv_ctrl_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_ctrl_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_ctrl_cb_Invoke(
        zan_wv_ctrl_cb *self, HRESULT hr, ICoreWebView2Controller *ctrl) {
    self->hr = hr;
    if (SUCCEEDED(hr) && ctrl) {
        self->inst->ctrl = ctrl;
        ctrl->lpVtbl->AddRef(ctrl);
        ICoreWebView2 *core = NULL;
        if (SUCCEEDED(ctrl->lpVtbl->get_CoreWebView2(ctrl, &core)) && core) {
            self->inst->core = core;
        }
    }
    self->done = 1;
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl
    g_wv_ctrl_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *, REFIID,
        void **))zan_wv_ctrl_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *))
        zan_wv_ctrl_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *))
        zan_wv_ctrl_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *, HRESULT,
        ICoreWebView2Controller *))zan_wv_ctrl_cb_Invoke
};

/* -- navigation starting (records the request URL) -- */
typedef struct {
    ICoreWebView2NavigationStartingEventHandlerVtbl *vtbl;
    long refs;
    zan_wv *inst;
} zan_wv_start_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_start_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_start_cb_Invoke(
        zan_wv_start_cb *self, ICoreWebView2 *sender,
        ICoreWebView2NavigationStartingEventArgs *args) {
    (void)sender;
    LPWSTR uri = NULL;
    if (args && SUCCEEDED(args->lpVtbl->get_Uri(args, &uri)) && uri) {
        zan_wv_narrow_into(uri, self->inst->last_req, WV_STR);
        CoTaskMemFree(uri);
    }
    self->inst->loading = 1;
    return S_OK;
}

static ICoreWebView2NavigationStartingEventHandlerVtbl g_wv_start_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2NavigationStartingEventHandler *,
                                   REFIID, void **))zan_wv_start_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2NavigationStartingEventHandler *))
        zan_wv_start_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2NavigationStartingEventHandler *))
        zan_wv_start_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2NavigationStartingEventHandler *,
                                   ICoreWebView2 *,
                                   ICoreWebView2NavigationStartingEventArgs *))
        zan_wv_start_cb_Invoke
};

/* -- navigation completed (bumps the sequence the widget polls) -- */
typedef struct {
    ICoreWebView2NavigationCompletedEventHandlerVtbl *vtbl;
    long refs;
    zan_wv *inst;
} zan_wv_nav_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_nav_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_nav_cb_Invoke(
        zan_wv_nav_cb *self, ICoreWebView2 *sender,
        ICoreWebView2NavigationCompletedEventArgs *args) {
    zan_wv *w = self->inst;
    if (args) {
        COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        BOOL ok = FALSE;
        args->lpVtbl->get_IsSuccess(args, &ok);
        args->lpVtbl->get_WebErrorStatus(args, &st);
        /* The widget reports an HTTP-ish status: 200 for a successful load,
         * otherwise 400 + the WebView2 error enum so callers can tell failures
         * apart without a WebView2-specific vocabulary. */
        w->last_status = ok ? 200 : (400 + (int)st);
    }
    if (sender) {
        LPWSTR s = NULL;
        if (SUCCEEDED(sender->lpVtbl->get_Source(sender, &s)) && s) {
            zan_wv_narrow_into(s, w->url, WV_STR);
            CoTaskMemFree(s);
        }
        LPWSTR t = NULL;
        if (SUCCEEDED(sender->lpVtbl->get_DocumentTitle(sender, &t)) && t) {
            zan_wv_narrow_into(t, w->title, WV_STR);
            CoTaskMemFree(t);
        }
    }
    w->loading = 0;
    w->nav_seq++;
    return S_OK;
}

static ICoreWebView2NavigationCompletedEventHandlerVtbl g_wv_nav_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2NavigationCompletedEventHandler *,
                                   REFIID, void **))zan_wv_nav_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2NavigationCompletedEventHandler *))
        zan_wv_nav_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2NavigationCompletedEventHandler *))
        zan_wv_nav_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2NavigationCompletedEventHandler *,
                                   ICoreWebView2 *,
                                   ICoreWebView2NavigationCompletedEventArgs *))
        zan_wv_nav_cb_Invoke
};

/* -- source changed (SPA navigations keep Url() honest) -- */
typedef struct {
    ICoreWebView2SourceChangedEventHandlerVtbl *vtbl;
    long refs;
    zan_wv *inst;
} zan_wv_src_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_src_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_src_cb_Invoke(
        zan_wv_src_cb *self, ICoreWebView2 *sender,
        ICoreWebView2SourceChangedEventArgs *args) {
    (void)args;
    if (sender) {
        LPWSTR s = NULL;
        if (SUCCEEDED(sender->lpVtbl->get_Source(sender, &s)) && s) {
            zan_wv_narrow_into(s, self->inst->url, WV_STR);
            CoTaskMemFree(s);
        }
    }
    self->inst->nav_seq++;
    return S_OK;
}

static ICoreWebView2SourceChangedEventHandlerVtbl g_wv_src_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2SourceChangedEventHandler *,
                                   REFIID, void **))zan_wv_src_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2SourceChangedEventHandler *))
        zan_wv_src_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2SourceChangedEventHandler *))
        zan_wv_src_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2SourceChangedEventHandler *,
                                   ICoreWebView2 *,
                                   ICoreWebView2SourceChangedEventArgs *))
        zan_wv_src_cb_Invoke
};

/* -- ExecuteScript completed -- */
typedef struct {
    ICoreWebView2ExecuteScriptCompletedHandlerVtbl *vtbl;
    long refs;
    volatile int done;
    char *result;
} zan_wv_script_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_script_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_script_cb_Invoke(
        zan_wv_script_cb *self, HRESULT hr, LPCWSTR json) {
    if (SUCCEEDED(hr) && json) self->result = zan_wv_narrow_dup(json);
    self->done = 1;
    return S_OK;
}

static ICoreWebView2ExecuteScriptCompletedHandlerVtbl g_wv_script_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2ExecuteScriptCompletedHandler *,
                                   REFIID, void **))zan_wv_script_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2ExecuteScriptCompletedHandler *))
        zan_wv_script_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2ExecuteScriptCompletedHandler *))
        zan_wv_script_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2ExecuteScriptCompletedHandler *,
                                   HRESULT, LPCWSTR))zan_wv_script_cb_Invoke
};

/* -- GetCookies completed -- */
typedef struct {
    ICoreWebView2GetCookiesCompletedHandlerVtbl *vtbl;
    long refs;
    volatile int done;
    char *result;
} zan_wv_cookie_cb;

ZAN_WV_UNKNOWN_IMPL(zan_wv_cookie_cb)

static HRESULT STDMETHODCALLTYPE zan_wv_cookie_cb_Invoke(
        zan_wv_cookie_cb *self, HRESULT hr, ICoreWebView2CookieList *list) {
    if (SUCCEEDED(hr) && list) {
        UINT n = 0;
        list->lpVtbl->get_Count(list, &n);
        size_t cap = 256;
        char *buf = (char *)malloc(cap);
        if (buf) {
            buf[0] = '\0';
            size_t len = 0;
            for (UINT i = 0; i < n; i++) {
                ICoreWebView2Cookie *ck = NULL;
                if (FAILED(list->lpVtbl->GetValueAtIndex(list, i, &ck)) || !ck)
                    continue;
                LPWSTR wn = NULL, wv = NULL;
                ck->lpVtbl->get_Name(ck, &wn);
                ck->lpVtbl->get_Value(ck, &wv);
                char *cn = zan_wv_narrow_dup(wn);
                char *cv = zan_wv_narrow_dup(wv);
                if (cn) {
                    size_t need = len + strlen(cn) + (cv ? strlen(cv) : 0) + 4;
                    if (need > cap) {
                        while (cap < need) cap *= 2;
                        char *nb = (char *)realloc(buf, cap);
                        if (nb) buf = nb; else need = 0;
                    }
                    if (need) {
                        if (len) { strcpy(buf + len, "; "); len += 2; }
                        strcpy(buf + len, cn); len += strlen(cn);
                        buf[len++] = '=';
                        if (cv) { strcpy(buf + len, cv); len += strlen(cv); }
                        buf[len] = '\0';
                    }
                }
                free(cn); free(cv);
                if (wn) CoTaskMemFree(wn);
                if (wv) CoTaskMemFree(wv);
                ck->lpVtbl->Release(ck);
            }
            self->result = buf;
        }
    }
    self->done = 1;
    return S_OK;
}

static ICoreWebView2GetCookiesCompletedHandlerVtbl g_wv_cookie_cb_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2GetCookiesCompletedHandler *,
                                   REFIID, void **))zan_wv_cookie_cb_QI,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2GetCookiesCompletedHandler *))
        zan_wv_cookie_cb_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(ICoreWebView2GetCookiesCompletedHandler *))
        zan_wv_cookie_cb_Release,
    (HRESULT (STDMETHODCALLTYPE *)(ICoreWebView2GetCookiesCompletedHandler *,
                                   HRESULT, ICoreWebView2CookieList *))
        zan_wv_cookie_cb_Invoke
};

/* ---------------- loader + environment ---------------- */

/* Resolves CreateCoreWebView2EnvironmentWithOptions from WebView2Loader.dll,
 * looked for beside the executable / on PATH first and then beside this
 * module (the driver directory zanc bundles into the output folder). */
static int zan_wv_load_loader(void) {
    if (g_wv_create_env) return 1;
    if (!g_wv_loader) {
        g_wv_loader = LoadLibraryW(L"WebView2Loader.dll");
    }
    if (!g_wv_loader) {
        wchar_t self_path[MAX_PATH];
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)&zan_wv_load_loader, &self);
        DWORD n = GetModuleFileNameW(self, self_path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            wchar_t *slash = wcsrchr(self_path, L'\\');
            if (slash) {
                slash[1] = L'\0';
                wchar_t cand[MAX_PATH];
                if (wcslen(self_path) + 20 < MAX_PATH) {
                    wcscpy(cand, self_path);
                    wcscat(cand, L"WebView2Loader.dll");
                    g_wv_loader = LoadLibraryW(cand);
                }
            }
        }
    }
    if (!g_wv_loader) return 0;
    g_wv_create_env = (zan_wv_create_env_fn)(void *)GetProcAddress(
        g_wv_loader, "CreateCoreWebView2EnvironmentWithOptions");
    return g_wv_create_env != NULL;
}

/* Per-profile user data folder: %LOCALAPPDATA%\ZanGui\WebView2\<profile>.
 * Views sharing a profile id share cookies and storage; distinct ids are
 * isolated because WebView2 partitions by this folder. */
static wchar_t *zan_wv_profile_dir(const char *profile_id) {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetEnvironmentVariableW(L"TEMP", base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return NULL;
    }
    const char *id = (profile_id && profile_id[0]) ? profile_id : "default";
    wchar_t *wid = zan_wv_wide(id);
    if (!wid) return NULL;
    /* keep the folder name a safe single path segment */
    for (wchar_t *p = wid; *p; p++) {
        if (!iswalnum(*p) && *p != L'-' && *p != L'_') *p = L'_';
    }
    size_t need = wcslen(base) + wcslen(wid) + 32;
    wchar_t *dir = (wchar_t *)malloc(need * sizeof(wchar_t));
    if (!dir) { free(wid); return NULL; }
    wcscpy(dir, base);
    wcscat(dir, L"\\ZanGui\\WebView2\\");
    wcscat(dir, wid);
    free(wid);
    return dir;
}

EXPORT i64 zan_gui_webview_create(i64 hwnd, const char *profile_id) {
    if (!hwnd) return 0;
    if (!zan_wv_load_loader()) return 0;

    int slot = -1;
    for (int i = 0; i < WV_MAX; i++) { if (!g_wv[i].used) { slot = i; break; } }
    if (slot < 0) return 0;

    zan_wv *w = &g_wv[slot];
    memset(w, 0, sizeof(*w));
    w->used = 1;
    w->hwnd = (HWND)(intptr_t)hwnd;

    if (!g_wv_com_init) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        g_wv_com_init = 1;
    }

    zan_wv_env_cb *ecb = (zan_wv_env_cb *)calloc(1, sizeof(zan_wv_env_cb));
    if (!ecb) { w->used = 0; return 0; }
    ecb->vtbl = &g_wv_env_cb_vtbl;
    ecb->refs = 1;
    ecb->inst = w;

    wchar_t *dir = zan_wv_profile_dir(profile_id);
    HRESULT hr = g_wv_create_env(
        NULL, dir, NULL,
        (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)ecb);
    free(dir);
    if (FAILED(hr)) {
        ecb->vtbl->Release(
            (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)ecb);
        w->used = 0;
        return 0;
    }
    zan_wv_pump(&ecb->done, 8000);
    int env_ok = w->env != NULL;
    ecb->vtbl->Release(
        (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)ecb);
    if (!env_ok) { w->used = 0; return 0; }

    zan_wv_ctrl_cb *ccb = (zan_wv_ctrl_cb *)calloc(1, sizeof(zan_wv_ctrl_cb));
    if (!ccb) { w->env->lpVtbl->Release(w->env); w->used = 0; return 0; }
    ccb->vtbl = &g_wv_ctrl_cb_vtbl;
    ccb->refs = 1;
    ccb->inst = w;
    hr = w->env->lpVtbl->CreateCoreWebView2Controller(
        w->env, w->hwnd,
        (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)ccb);
    if (SUCCEEDED(hr)) zan_wv_pump(&ccb->done, 8000);
    ccb->vtbl->Release(
        (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)ccb);

    if (!w->core) {
        if (w->ctrl) w->ctrl->lpVtbl->Release(w->ctrl);
        w->env->lpVtbl->Release(w->env);
        memset(w, 0, sizeof(*w));
        return 0;
    }

    zan_wv_nav_cb *ncb = (zan_wv_nav_cb *)calloc(1, sizeof(zan_wv_nav_cb));
    if (ncb) {
        ncb->vtbl = &g_wv_nav_cb_vtbl;
        ncb->refs = 1;
        ncb->inst = w;
        w->core->lpVtbl->add_NavigationCompleted(
            w->core, (ICoreWebView2NavigationCompletedEventHandler *)ncb,
            &w->nav_tok);
        ncb->vtbl->Release((ICoreWebView2NavigationCompletedEventHandler *)ncb);
    }
    zan_wv_start_cb *scb = (zan_wv_start_cb *)calloc(1, sizeof(zan_wv_start_cb));
    if (scb) {
        scb->vtbl = &g_wv_start_cb_vtbl;
        scb->refs = 1;
        scb->inst = w;
        w->core->lpVtbl->add_NavigationStarting(
            w->core, (ICoreWebView2NavigationStartingEventHandler *)scb,
            &w->start_tok);
        scb->vtbl->Release((ICoreWebView2NavigationStartingEventHandler *)scb);
    }
    zan_wv_src_cb *rcb = (zan_wv_src_cb *)calloc(1, sizeof(zan_wv_src_cb));
    if (rcb) {
        rcb->vtbl = &g_wv_src_cb_vtbl;
        rcb->refs = 1;
        rcb->inst = w;
        w->core->lpVtbl->add_SourceChanged(
            w->core, (ICoreWebView2SourceChangedEventHandler *)rcb, &w->src_tok);
        rcb->vtbl->Release((ICoreWebView2SourceChangedEventHandler *)rcb);
    }

    /* Starts hidden and zero-sized; the widget drives frame + visibility every
     * frame from the region it occupies. */
    RECT rc = { 0, 0, 0, 0 };
    w->ctrl->lpVtbl->put_Bounds(w->ctrl, rc);
    w->ctrl->lpVtbl->put_IsVisible(w->ctrl, FALSE);
    return slot + 1;
}

EXPORT void zan_gui_webview_destroy(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return;
    if (w->core) {
        w->core->lpVtbl->remove_NavigationCompleted(w->core, w->nav_tok);
        w->core->lpVtbl->remove_NavigationStarting(w->core, w->start_tok);
        w->core->lpVtbl->remove_SourceChanged(w->core, w->src_tok);
        w->core->lpVtbl->Release(w->core);
    }
    if (w->ctrl) {
        w->ctrl->lpVtbl->Close(w->ctrl);
        w->ctrl->lpVtbl->Release(w->ctrl);
    }
    if (w->env) w->env->lpVtbl->Release(w->env);
    free(w->eval_out);
    free(w->cookie_out);
    memset(w, 0, sizeof(*w));
}

EXPORT void zan_gui_webview_set_frame(i64 h, i64 x, i64 y, i64 ww, i64 hh) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->ctrl) return;
    RECT rc;
    rc.left = (LONG)x;
    rc.top = (LONG)y;
    rc.right = (LONG)(x + ww);
    rc.bottom = (LONG)(y + hh);
    w->ctrl->lpVtbl->put_Bounds(w->ctrl, rc);
}

EXPORT void zan_gui_webview_set_visible(i64 h, i64 visible) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->ctrl) return;
    w->ctrl->lpVtbl->put_IsVisible(w->ctrl, visible ? TRUE : FALSE);
}

EXPORT void zan_gui_webview_navigate(i64 h, const char *url) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->core) return;
    wchar_t *wu = zan_wv_wide(url);
    if (!wu) return;
    w->core->lpVtbl->Navigate(w->core, wu);
    free(wu);
}

EXPORT void zan_gui_webview_load_html(i64 h, const char *html,
                                      const char *base_url) {
    (void)base_url;   /* WebView2 has no base-url form of NavigateToString */
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->core) return;
    wchar_t *wh = zan_wv_wide(html);
    if (!wh) return;
    w->core->lpVtbl->NavigateToString(w->core, wh);
    free(wh);
}

EXPORT void zan_gui_webview_back(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (w && w->core) w->core->lpVtbl->GoBack(w->core);
}

EXPORT void zan_gui_webview_forward(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (w && w->core) w->core->lpVtbl->GoForward(w->core);
}

EXPORT void zan_gui_webview_reload(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (w && w->core) w->core->lpVtbl->Reload(w->core);
}

EXPORT void zan_gui_webview_stop(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (w && w->core) w->core->lpVtbl->Stop(w->core);
}

EXPORT i64 zan_gui_webview_can_go_back(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->core) return 0;
    BOOL can = FALSE;
    w->core->lpVtbl->get_CanGoBack(w->core, &can);
    return can ? 1 : 0;
}

EXPORT i64 zan_gui_webview_can_go_forward(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->core) return 0;
    BOOL can = FALSE;
    w->core->lpVtbl->get_CanGoForward(w->core, &can);
    return can ? 1 : 0;
}

EXPORT i64 zan_gui_webview_is_loading(i64 h) {
    zan_wv *w = zan_wv_get(h);
    return (w && w->loading) ? 1 : 0;
}

EXPORT i64 zan_gui_webview_nav_seq(i64 h) {
    zan_wv *w = zan_wv_get(h);
    return w ? w->nav_seq : 0;
}

EXPORT i64 zan_gui_webview_last_status(i64 h) {
    zan_wv *w = zan_wv_get(h);
    return w ? w->last_status : 0;
}

EXPORT const char *zan_gui_webview_get_url(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return "";
    if (w->core) {
        LPWSTR s = NULL;
        if (SUCCEEDED(w->core->lpVtbl->get_Source(w->core, &s)) && s) {
            zan_wv_narrow_into(s, w->url, WV_STR);
            CoTaskMemFree(s);
        }
    }
    return w->url;
}

EXPORT const char *zan_gui_webview_get_title(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return "";
    if (w->core) {
        LPWSTR s = NULL;
        if (SUCCEEDED(w->core->lpVtbl->get_DocumentTitle(w->core, &s)) && s) {
            zan_wv_narrow_into(s, w->title, WV_STR);
            CoTaskMemFree(s);
        }
    }
    return w->title;
}

EXPORT const char *zan_gui_webview_last_request(i64 h) {
    zan_wv *w = zan_wv_get(h);
    return w ? w->last_req : "";
}

EXPORT const char *zan_gui_webview_eval(i64 h, const char *js) {
    zan_wv *w = zan_wv_get(h);
    if (!w || !w->core) return "";
    wchar_t *wjs = zan_wv_wide(js);
    if (!wjs) return "";
    zan_wv_script_cb *cb =
        (zan_wv_script_cb *)calloc(1, sizeof(zan_wv_script_cb));
    if (!cb) { free(wjs); return ""; }
    cb->vtbl = &g_wv_script_cb_vtbl;
    cb->refs = 1;
    HRESULT hr = w->core->lpVtbl->ExecuteScript(
        w->core, wjs, (ICoreWebView2ExecuteScriptCompletedHandler *)cb);
    free(wjs);
    if (SUCCEEDED(hr)) zan_wv_pump(&cb->done, 5000);
    free(w->eval_out);
    w->eval_out = cb->result;   /* ownership moves to the instance */
    cb->result = NULL;
    cb->vtbl->Release((ICoreWebView2ExecuteScriptCompletedHandler *)cb);
    return w->eval_out ? w->eval_out : "";
}

/* ICoreWebView2_2 carries the cookie manager; it is present on every runtime
 * new enough to matter, but a missing QueryInterface just yields no cookies. */
static ICoreWebView2CookieManager *zan_wv_cookies(zan_wv *w) {
    if (!w || !w->core) return NULL;
    ICoreWebView2_2 *c2 = NULL;
    if (FAILED(w->core->lpVtbl->QueryInterface(w->core, &IID_ICoreWebView2_2,
                                               (void **)&c2)) || !c2)
        return NULL;
    ICoreWebView2CookieManager *cm = NULL;
    c2->lpVtbl->get_CookieManager(c2, &cm);
    c2->lpVtbl->Release(c2);
    return cm;
}

EXPORT const char *zan_gui_webview_get_cookies(i64 h, const char *url) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return "";
    ICoreWebView2CookieManager *cm = zan_wv_cookies(w);
    if (!cm) return "";
    zan_wv_cookie_cb *cb =
        (zan_wv_cookie_cb *)calloc(1, sizeof(zan_wv_cookie_cb));
    if (!cb) { cm->lpVtbl->Release(cm); return ""; }
    cb->vtbl = &g_wv_cookie_cb_vtbl;
    cb->refs = 1;
    wchar_t *wu = (url && url[0]) ? zan_wv_wide(url) : NULL;
    HRESULT hr = cm->lpVtbl->GetCookies(
        cm, wu, (ICoreWebView2GetCookiesCompletedHandler *)cb);
    free(wu);
    if (SUCCEEDED(hr)) zan_wv_pump(&cb->done, 5000);
    free(w->cookie_out);
    w->cookie_out = cb->result;
    cb->result = NULL;
    cb->vtbl->Release((ICoreWebView2GetCookiesCompletedHandler *)cb);
    cm->lpVtbl->Release(cm);
    return w->cookie_out ? w->cookie_out : "";
}

EXPORT void zan_gui_webview_set_cookie(i64 h, const char *url,
                                       const char *name, const char *value) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return;
    ICoreWebView2CookieManager *cm = zan_wv_cookies(w);
    if (!cm) return;
    /* WebView2 stores cookies against a host + path rather than a URL, so the
     * host is taken from `url` (scheme and any path/query stripped) and the
     * cookie is written at the site root. */
    char host[512];
    host[0] = '\0';
    if (url) {
        const char *p = strstr(url, "://");
        p = p ? p + 3 : url;
        int i = 0;
        while (p[i] && p[i] != '/' && p[i] != '?' && p[i] != '#' &&
               i < (int)sizeof(host) - 1) {
            host[i] = p[i];
            i++;
        }
        host[i] = '\0';
        char *colon = strchr(host, ':');
        if (colon) *colon = '\0';
    }
    wchar_t *wn = zan_wv_wide(name);
    wchar_t *wv = zan_wv_wide(value);
    wchar_t *wh = zan_wv_wide(host);
    ICoreWebView2Cookie *ck = NULL;
    if (wn && wv && wh && host[0] &&
        SUCCEEDED(cm->lpVtbl->CreateCookie(cm, wn, wv, wh, L"/", &ck)) && ck) {
        cm->lpVtbl->AddOrUpdateCookie(cm, ck);
        ck->lpVtbl->Release(ck);
    }
    free(wn); free(wv); free(wh);
    cm->lpVtbl->Release(cm);
}

EXPORT void zan_gui_webview_clear_cookies(i64 h) {
    zan_wv *w = zan_wv_get(h);
    if (!w) return;
    ICoreWebView2CookieManager *cm = zan_wv_cookies(w);
    if (!cm) return;
    cm->lpVtbl->DeleteAllCookies(cm);
    cm->lpVtbl->Release(cm);
}
