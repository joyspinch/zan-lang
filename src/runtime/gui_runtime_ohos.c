/* gui_runtime_ohos */

#ifdef ZAN_GUI_OHOS

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <native_window/external_window.h>

/* 内部辅助实现 */
typedef int (*FnNWRequestBuffer)(OHNativeWindow *, OHNativeWindowBuffer **, int *);
typedef int (*FnNWFlushBuffer)(OHNativeWindow *, OHNativeWindowBuffer *, int, Region *);
typedef BufferHandle *(*FnNWGetBufferHandle)(OHNativeWindowBuffer *);
typedef int (*FnNWHandleOpt)(OHNativeWindow *, int, ...);
static FnNWRequestBuffer   g_nw_request;
static FnNWFlushBuffer     g_nw_flush;
static FnNWGetBufferHandle g_nw_getbh;
static FnNWHandleOpt       g_nw_handleopt;

/* 内部辅助实现 */
typedef struct {
    void *nw;             /* 底层系统交互与数据协议契约 */
    int w, h;             /* 核心系统底层抽象与内存语义契约 */
    int attached;
    int closed;           /* 核心系统底层抽象与内存语义契约 */

    /* OpenHarmony EGL 呈现状态：在 surface 附着或首次 present 时创建 */
    EGLDisplay egl_dpy;
    EGLSurface egl_surf;
    void      *surf_nw;   /* 底层系统交互与数据协议契约 */
    int        surf_w, surf_h; /* 内部辅助实现 */
    EGLContext egl_ctx;
    GLuint     gl_prog;
    GLuint     gl_tex;
    GLuint     gl_vbo;
    int        tex_w, tex_h;
} zan_ohos_win_t;
static zan_ohos_win_t g_owin;
#define ZAN_OHOS_HWND ((iptr)&g_owin)

static int  g_window_width  = 0;
static int  g_window_height = 0;
static int  g_dpi           = 96;

/* 内部辅助实现 */
typedef struct { int e[8]; iptr win; } zan_oev_t;
#define ZAN_OQ_CAP 512
static zan_oev_t g_oq[ZAN_OQ_CAP];
static int g_oq_head = 0, g_oq_tail = 0;
static pthread_mutex_t g_oq_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_pending_event[8];
static iptr g_event_win = 0;
static long long g_ev_seq = 0;

/* 触控移动事件合并：取最新坐标；滚轮滑动事件合并：累加偏移量 */
static void oq_push_locked(int kind, int x, int y, int button, int code, int mods) {
    int last = (g_oq_tail + ZAN_OQ_CAP - 1) % ZAN_OQ_CAP;
    int has_last = (g_oq_head != g_oq_tail);
    if (kind == 1 && has_last && g_oq[last].e[0] == 1) {
        g_oq[last].e[1] = x; g_oq[last].e[2] = y;
        return;
    }
    if (kind == 13 && has_last && g_oq[last].e[0] == 13) {
        g_oq[last].e[1] = x; g_oq[last].e[2] = y;
        g_oq[last].e[4] += code;
        return;
    }
    int next = (g_oq_tail + 1) % ZAN_OQ_CAP;
    if (next == g_oq_head) return; /* full: drop */
    zan_oev_t *z = &g_oq[g_oq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = 0; z->e[7] = 0;
    z->win = ZAN_OHOS_HWND;
    g_oq_tail = next;
}

/* 触控抬起派发：设置吞掉点击标记以防拖拽误判为点击 */
static void oq_push_flag_locked(int kind, int x, int y, int button, int code, int mods) {
    oq_push_locked(kind, x, y, button, code, mods);
    if (g_oq_head != g_oq_tail) {
        int last = (g_oq_tail + ZAN_OQ_CAP - 1) % ZAN_OQ_CAP;
        g_oq[last].e[6] = 1;
    }
}

/* XComponent 回调桥接（接收 ArkTS 宿主分发的生命周期与触控事件） */

EXPORT void zan_gui_ohos_attach(void *nw, int w, int h) {
    if (!nw || w <= 0 || h <= 0) return;
    pthread_mutex_lock(&g_oq_lock);
    g_owin.nw = nw;
    g_owin.w = w; g_owin.h = h;
    g_owin.attached = 1;
    g_window_width = w;
    g_window_height = h;
    oq_push_locked(7, w, h, 0, 0, 0);
    /* 内部辅助实现 */
    oq_push_locked(14, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);

    /* 特性探测：真机优先使用 BufferQueue 直写，模拟器回退 EGL 呈现 */
    if (!g_nw_request) {
        g_nw_request = (FnNWRequestBuffer)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowRequestBuffer");
        g_nw_flush = (FnNWFlushBuffer)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowFlushBuffer");
        g_nw_getbh = (FnNWGetBufferHandle)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_GetBufferHandleFromNative");
        g_nw_handleopt = (FnNWHandleOpt)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowHandleOpt");
    }
    if (g_nw_request && g_nw_handleopt) {
        /* 窗口重附着或旋转：重新配置窗口像素格式 (WINDOW_FORMAT_RGBA_8888) */
        int32_t fmt = 2; /* NATIVEBUFFER_PIXEL_FMT_BGRA_8888 */
        g_nw_handleopt((OHNativeWindow *)nw, SET_FORMAT, fmt);
    }
}

/* 进程切后台 Surface 销毁生命周期处理 */
EXPORT void zan_gui_ohos_detach(void) {
    pthread_mutex_lock(&g_oq_lock);
    g_owin.attached = 0;
    g_owin.nw = NULL;
    pthread_mutex_unlock(&g_oq_lock);
}

/* 内部辅助实现 */
static i64 ohs_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define OH_TOUCH_SLOP2   64.0f   /* 核心系统底层抽象与内存语义契约 */
#define OH_TOUCH_HIST    8       /* 核心系统底层抽象与内存语义契约 */
#define OH_FLING_START_PX_S 250.0f
#define OH_FLING_STOP_PX_S  120.0f
#define OH_FLING_TAU_MS     400.0f

static int   g_tg_down, g_tg_drag;
static float g_tg_ax, g_tg_ay;    /* 核心系统底层抽象与内存语义契约 */
static float g_tg_x, g_tg_y;      /* 核心系统底层抽象与内存语义契约 */
static float g_tg_acc;            /* 核心系统底层抽象与内存语义契约 */
static long long g_tg_ht[OH_TOUCH_HIST];
static float g_tg_hy[OH_TOUCH_HIST];
static int   g_tg_hn, g_tg_hi;

static int g_fling_active;
static float g_fling_v;           /* 核心系统底层抽象与内存语义契约 */
static float g_fling_acc;
static int   g_fling_x, g_fling_y;
static long long g_fling_last;
static pthread_t g_fling_thr;
static int g_fling_thr_up;

static void *fling_thread(void *arg) {
    (void)arg;
    for (;;) {
        int live;
        pthread_mutex_lock(&g_oq_lock);
        live = g_fling_active;
        if (live) {
            long long now = ohs_tick_ms();
            float dt = (float)(now - g_fling_last) / 1000.0f;
            g_fling_last = now;
            if (dt > 0) {
                g_fling_v *= expf(-dt * 1000.0f / OH_FLING_TAU_MS);
                g_fling_acc += g_fling_v * dt * 288.0f / (float)g_dpi;
                int delta = (int)g_fling_acc;
                if (delta != 0) {
                    g_fling_acc -= (float)delta;
                    oq_push_locked(13, g_fling_x, g_fling_y, 0, delta, 0);
                }
            }
            if (fabsf(g_fling_v) < OH_FLING_STOP_PX_S) g_fling_active = 0;
        }
        pthread_mutex_unlock(&g_oq_lock);
        if (!live) break;
        struct timespec ts = { 0, 16 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    g_fling_thr_up = 0;
    return NULL;
}

static void fling_cancel_locked(void) { g_fling_active = 0; }

static void fling_start(float v, int x, int y) {
    if (fabsf(v) < OH_FLING_START_PX_S) return;
    pthread_mutex_lock(&g_oq_lock);
    g_fling_v = v;
    g_fling_acc = 0;
    g_fling_x = x; g_fling_y = y;
    g_fling_last = ohs_tick_ms();
    g_fling_active = 1;
    int need = !g_fling_thr_up;
    g_fling_thr_up = 1;
    pthread_mutex_unlock(&g_oq_lock);
    if (need && pthread_create(&g_fling_thr, NULL, fling_thread, NULL) != 0)
        g_fling_thr_up = 0;
}

EXPORT void zan_gui_ohos_touch(int action, int x, int y) {
    /* action: 0 = down, 1 = move, 2 = up. */
    static int px = 0, py = 0;
    if (x < 0 && y < 0) { x = px; y = py; }
    px = x; py = y;

    if (action == 0) {
        pthread_mutex_lock(&g_oq_lock);
        fling_cancel_locked(); /* 底层系统交互与数据协议契约 */
        g_tg_down = 1; g_tg_drag = 0; g_tg_acc = 0;
        g_tg_x = g_tg_ax = (float)x;
        g_tg_y = g_tg_ay = (float)y;
        g_tg_hn = 1; g_tg_hi = 1 % OH_TOUCH_HIST;
        g_tg_ht[0] = ohs_tick_ms(); g_tg_hy[0] = g_tg_y;
        /* 内部辅助实现 */
        oq_push_locked(1, (int)g_tg_ax, (int)g_tg_ay, 0, 0, 0);
        oq_push_locked(2, (int)g_tg_ax, (int)g_tg_ay, 0, 0, 0);
        pthread_mutex_unlock(&g_oq_lock);
        return;
    }
    if (action == 1 && g_tg_down) {
        pthread_mutex_lock(&g_oq_lock);
        float fx = (float)x, fy = (float)y;
        if (!g_tg_drag) {
            float dx = fx - g_tg_ax, dy = fy - g_tg_ay;
            if (dx * dx + dy * dy < OH_TOUCH_SLOP2) {
                pthread_mutex_unlock(&g_oq_lock);
                return; /* slop: still a tap */
            }
            g_tg_drag = 1;
            /* 内部辅助实现 */
        }
        /* 内部辅助实现 */
        g_tg_acc += (fy - g_tg_y) * 288.0f / (float)g_dpi;
        int delta = (int)g_tg_acc;
        if (delta != 0) {
            g_tg_acc -= (float)delta;
            oq_push_locked(13, x, y, 0, delta, 0);
        }
        g_tg_ht[g_tg_hi] = ohs_tick_ms(); g_tg_hy[g_tg_hi] = fy;
        g_tg_hi = (g_tg_hi + 1) % OH_TOUCH_HIST;
        if (g_tg_hn < OH_TOUCH_HIST) g_tg_hn++;
        g_tg_x = fx; g_tg_y = fy;
        /* 触控滚轮事件转换与光标坐标同步更新 */
        if (delta == 0) oq_push_flag_locked(1, x, y, 0, 0, 0);
        pthread_mutex_unlock(&g_oq_lock);
        return;
    }
    if (action == 2 && g_tg_down) {
        g_tg_down = 0;
        pthread_mutex_lock(&g_oq_lock);
        if (g_tg_drag) {
            /* 内部辅助实现 */
            oq_push_flag_locked(3, (int)g_tg_x, (int)g_tg_y, 0, 0, 0);
            pthread_mutex_unlock(&g_oq_lock);
            /* 计算滑动释放速度 (~120ms 时间窗口滑动惯性) */
            int last = (g_tg_hi + OH_TOUCH_HIST - 1) % OH_TOUCH_HIST;
            int old = last;
            for (int k = 0; k < g_tg_hn; k++) {
                int idx = (last - k + OH_TOUCH_HIST) % OH_TOUCH_HIST;
                if (g_tg_ht[idx] + 120 < g_tg_ht[last]) break;
                old = idx;
            }
            long long span = g_tg_ht[last] - g_tg_ht[old];
            if (span > 0 && g_tg_hn >= 2) {
                float v = (g_tg_hy[last] - g_tg_hy[old]) * 1000.0f
                          / (float)span;
                fling_start(v, (int)g_tg_x, (int)g_tg_y);
            }
        } else {
            /* 单击事件完成：手指按下时已发送位置与按下事件，此处仅发送抬起 */
            int ax = (int)g_tg_ax, ay = (int)g_tg_ay;
            oq_push_locked(3, ax, ay, 0, 0, 0);
            pthread_mutex_unlock(&g_oq_lock);
        }
        return;
    }
}

/* 内部辅助实现 */

/* 包含 uchar.h 以支持 IMF 头文件中的 char16_t 类型定义 */
#include <uchar.h>
#include "inputmethod/inputmethod_controller_capi.h"

typedef InputMethod_ErrorCode (*FnImeProxyCreate)(InputMethod_TextEditorProxy **);
typedef void (*FnImeProxyDestroy)(InputMethod_TextEditorProxy *);
#define IME_PROXY_SETTER(name, field) \
    typedef InputMethod_ErrorCode (*FnImeSet##name)( \
        InputMethod_TextEditorProxy *, field);
IME_PROXY_SETTER(GetTextConfig, OH_TextEditorProxy_GetTextConfigFunc)
IME_PROXY_SETTER(InsertText, OH_TextEditorProxy_InsertTextFunc)
IME_PROXY_SETTER(DeleteForward, OH_TextEditorProxy_DeleteForwardFunc)
IME_PROXY_SETTER(DeleteBackward, OH_TextEditorProxy_DeleteBackwardFunc)
IME_PROXY_SETTER(SendEnterKey, OH_TextEditorProxy_SendEnterKeyFunc)
IME_PROXY_SETTER(MoveCursor, OH_TextEditorProxy_MoveCursorFunc)
IME_PROXY_SETTER(SendKeyboardStatus, OH_TextEditorProxy_SendKeyboardStatusFunc)
IME_PROXY_SETTER(HandleSetSelection, OH_TextEditorProxy_HandleSetSelectionFunc)
IME_PROXY_SETTER(HandleExtendAction, OH_TextEditorProxy_HandleExtendActionFunc)
IME_PROXY_SETTER(GetLeftTextOfCursor, OH_TextEditorProxy_GetLeftTextOfCursorFunc)
IME_PROXY_SETTER(GetRightTextOfCursor, OH_TextEditorProxy_GetRightTextOfCursorFunc)
IME_PROXY_SETTER(GetTextIndexAtCursor, OH_TextEditorProxy_GetTextIndexAtCursorFunc)
IME_PROXY_SETTER(ReceivePrivateCommand, OH_TextEditorProxy_ReceivePrivateCommandFunc)
IME_PROXY_SETTER(SetPreviewText, OH_TextEditorProxy_SetPreviewTextFunc)
IME_PROXY_SETTER(FinishTextPreview, OH_TextEditorProxy_FinishTextPreviewFunc)
typedef InputMethod_ErrorCode (*FnImeAttach)(InputMethod_TextEditorProxy *,
    InputMethod_AttachOptions *, InputMethod_InputMethodProxy **);
typedef InputMethod_ErrorCode (*FnImeDetach)(InputMethod_InputMethodProxy *);
typedef InputMethod_AttachOptions *(*FnImeAttachOptsCreate)(bool);
typedef void (*FnImeAttachOptsDestroy)(InputMethod_AttachOptions *);
typedef InputMethod_ErrorCode (*FnImeCfgSetInputType)(InputMethod_TextConfig *,
    InputMethod_TextInputType);
typedef InputMethod_ErrorCode (*FnImeCfgSetEnterKeyType)(InputMethod_TextConfig *,
    InputMethod_EnterKeyType);
typedef InputMethod_ErrorCode (*FnImeCfgSetPreviewSupport)(InputMethod_TextConfig *,
    bool);

static FnImeProxyCreate g_ime_proxy_create;
static FnImeProxyDestroy g_ime_proxy_destroy;
static FnImeSetGetTextConfig g_ime_set_cfg;
static FnImeSetInsertText g_ime_set_insert;
static FnImeSetDeleteForward g_ime_set_delfwd;
static FnImeSetDeleteBackward g_ime_set_delbwd;
static FnImeSetSendEnterKey g_ime_set_enter;
static FnImeSetMoveCursor g_ime_set_move;
static FnImeSetSendKeyboardStatus g_ime_set_kbstatus;
static FnImeSetHandleSetSelection g_ime_setsel;
static FnImeSetHandleExtendAction g_ime_setext;
static FnImeSetGetLeftTextOfCursor g_ime_set_left;
static FnImeSetGetRightTextOfCursor g_ime_set_right;
static FnImeSetGetTextIndexAtCursor g_ime_set_idx;
static FnImeSetReceivePrivateCommand g_ime_set_priv;
static FnImeSetSetPreviewText g_ime_set_prev;
static FnImeSetFinishTextPreview g_ime_set_finprev;
static FnImeAttach g_ime_attach;
static FnImeDetach g_ime_detach;
static FnImeAttachOptsCreate g_ime_opts_create;
static FnImeAttachOptsDestroy g_ime_opts_destroy;
static FnImeCfgSetInputType g_ime_cfg_input;
static FnImeCfgSetEnterKeyType g_ime_cfg_enter;
static FnImeCfgSetPreviewSupport g_ime_cfg_preview;

static InputMethod_TextEditorProxy *g_ime_proxy = NULL;
static InputMethod_InputMethodProxy *g_ime_im = NULL;
static int g_ime_attached = 0;

/* UTF-16 解码为 Unicode 码点（合并代理对） */
static int ime_utf16_next(const char16_t *s, size_t n, size_t *i) {
    if (*i >= n) { return 0; }
    unsigned int u = (unsigned int)s[(*i)++];
    if (u >= 0xD800 && u <= 0xDBFF && *i < n) {
        unsigned int lo = (unsigned int)s[*i];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
            (*i)++;
            return (int)(0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u));
        }
    }
    return (int)u;
}

/* OpenHarmony 输入法 (IMF) 回调：通过线程安全环形队列传递输入文本 */
static void ime_on_insert(InputMethod_TextEditorProxy *proxy,
                          const char16_t *text, size_t length) {
    (void)proxy;
    size_t i = 0;
    pthread_mutex_lock(&g_oq_lock);
    for (;;) {
        int cp = ime_utf16_next(text, length, &i);
        if (cp <= 0) { break; }
        if (cp == '\n') { cp = 13; }
        oq_push_locked(6, 0, 0, 0, cp, 0);
    }
    pthread_mutex_unlock(&g_oq_lock);
}

static void ime_on_delete_backward(InputMethod_TextEditorProxy *proxy,
                                   int32_t length) {
    (void)proxy;
    /* 文本长度按 UTF-16 单元计数，代理对视为单个码点 */
    int n = (length + 1) / 2;
    if (n < 1) { n = 1; }
    pthread_mutex_lock(&g_oq_lock);
    for (int k = 0; k < n; k++) { oq_push_locked(6, 0, 0, 0, 8, 0); }
    pthread_mutex_unlock(&g_oq_lock);
}

static void ime_on_delete_forward(InputMethod_TextEditorProxy *proxy,
                                  int32_t length) {
    (void)proxy;
    if (length < 1) { length = 1; }
    pthread_mutex_lock(&g_oq_lock);
    for (int32_t k = 0; k < length; k++) { oq_push_locked(4, 0, 0, 0, 46, 0); }
    pthread_mutex_unlock(&g_oq_lock);
}

static void ime_on_enter_key(InputMethod_TextEditorProxy *proxy,
                             InputMethod_EnterKeyType key) {
    (void)proxy;
    (void)key;
    pthread_mutex_lock(&g_oq_lock);
    oq_push_locked(6, 0, 0, 0, 13, 0);
    pthread_mutex_unlock(&g_oq_lock);
}

static void ime_on_move_cursor(InputMethod_TextEditorProxy *proxy,
                               InputMethod_Direction dir) {
    (void)proxy;
    int vk = 0;
    switch (dir) {
    case IME_DIRECTION_LEFT:  vk = 37; break;
    case IME_DIRECTION_UP:    vk = 38; break;
    case IME_DIRECTION_RIGHT: vk = 39; break;
    case IME_DIRECTION_DOWN:  vk = 40; break;
    default: return;
    }
    pthread_mutex_lock(&g_oq_lock);
    oq_push_locked(4, 0, 0, 0, vk, 0);
    pthread_mutex_unlock(&g_oq_lock);
}

/* 输入上下文查询空回退：光标位置与选区设为空 */
static void ime_on_get_left_text(InputMethod_TextEditorProxy *proxy,
                                 int32_t number, char16_t text[], size_t *length) {
    (void)proxy; (void)number;
    if (length) { *length = 0; }
    if (text) { text[0] = 0; }
}
static void ime_on_get_right_text(InputMethod_TextEditorProxy *proxy,
                                  int32_t number, char16_t text[], size_t *length) {
    (void)proxy; (void)number;
    if (length) { *length = 0; }
    if (text) { text[0] = 0; }
}
static int32_t ime_on_get_index_at_cursor(InputMethod_TextEditorProxy *proxy) {
    (void)proxy;
    return 0;
}
static int32_t ime_on_private_command(InputMethod_TextEditorProxy *proxy,
                                      InputMethod_PrivateCommand *cmd[], size_t size) {
    (void)proxy; (void)cmd; (void)size;
    return IME_ERR_OK;
}
static int32_t ime_on_set_preview_text(InputMethod_TextEditorProxy *proxy,
                                       const char16_t text[], size_t length,
                                       int32_t start, int32_t end) {
    (void)proxy; (void)text; (void)length; (void)start; (void)end;
    return IME_ERR_OK; /* 底层系统交互与数据协议契约 */
}

static void ime_on_keyboard_status(InputMethod_TextEditorProxy *proxy,
                                   InputMethod_KeyboardStatus status) {
    (void)proxy; (void)status;
}
static void ime_on_set_selection(InputMethod_TextEditorProxy *proxy,
                                 int32_t start, int32_t end) {
    (void)proxy; (void)start; (void)end;
}
static void ime_on_extend_action(InputMethod_TextEditorProxy *proxy,
                                 InputMethod_ExtendAction action) {
    (void)proxy; (void)action;
}
static void ime_on_finish_preview(InputMethod_TextEditorProxy *proxy) {
    (void)proxy;
}

static void ime_on_get_text_config(InputMethod_TextEditorProxy *proxy,
                                   InputMethod_TextConfig *config) {
    (void)proxy;
    /* 动态加载 libohinput.so 中的 TextConfig 配置接口 */
    if (g_ime_cfg_input) { g_ime_cfg_input(config, IME_TEXT_INPUT_TYPE_TEXT); }
    if (g_ime_cfg_preview) { g_ime_cfg_preview(config, false); }
    if (g_ime_cfg_enter) { g_ime_cfg_enter(config, IME_ENTER_KEY_UNSPECIFIED); }
}

static void ime_ensure_proxy(void) {
    if (g_ime_proxy || !g_ime_proxy_create) { return; }
    InputMethod_TextEditorProxy *p = NULL;
    if (g_ime_proxy_create(&p) != IME_ERR_OK || !p) { return; }
#define IME_SET(fn, field, val) \
    do { if (fn(p, val) != IME_ERR_OK) { goto fail; } } while (0)
    IME_SET(g_ime_set_cfg, GetTextConfigFunc, ime_on_get_text_config);
    IME_SET(g_ime_set_insert, InsertTextFunc, ime_on_insert);
    IME_SET(g_ime_set_delbwd, DeleteBackwardFunc, ime_on_delete_backward);
    IME_SET(g_ime_set_delfwd, DeleteForwardFunc, ime_on_delete_forward);
    IME_SET(g_ime_set_enter, SendEnterKeyFunc, ime_on_enter_key);
    IME_SET(g_ime_set_move, MoveCursorFunc, ime_on_move_cursor);
    IME_SET(g_ime_set_left, GetLeftTextOfCursorFunc, ime_on_get_left_text);
    IME_SET(g_ime_set_right, GetRightTextOfCursorFunc, ime_on_get_right_text);
    IME_SET(g_ime_set_idx, GetTextIndexAtCursorFunc, ime_on_get_index_at_cursor);
    IME_SET(g_ime_set_priv, ReceivePrivateCommandFunc, ime_on_private_command);
    IME_SET(g_ime_set_prev, SetPreviewTextFunc, ime_on_set_preview_text);
    IME_SET(g_ime_set_kbstatus, SendKeyboardStatusFunc, ime_on_keyboard_status);
    IME_SET(g_ime_setsel, HandleSetSelectionFunc, ime_on_set_selection);
    IME_SET(g_ime_setext, HandleExtendActionFunc, ime_on_extend_action);
    IME_SET(g_ime_set_finprev, FinishTextPreviewFunc, ime_on_finish_preview);
#undef IME_SET
    g_ime_proxy = p;
    return;
fail:
    g_ime_proxy_destroy(p);
}

static void ime_feature_detect(void) {
    static int done = 0;
    if (done) { return; }
    done = 1;
    void *h = dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_Create");
    if (!h) { return; } /* 核心系统底层抽象与内存语义契约 */
    g_ime_proxy_create = (FnImeProxyCreate)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_Create");
    g_ime_proxy_destroy = (FnImeProxyDestroy)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_Destroy");
    g_ime_set_cfg = (FnImeSetGetTextConfig)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetGetTextConfigFunc");
    g_ime_set_insert = (FnImeSetInsertText)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetInsertTextFunc");
    g_ime_set_delfwd = (FnImeSetDeleteForward)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetDeleteForwardFunc");
    g_ime_set_delbwd = (FnImeSetDeleteBackward)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetDeleteBackwardFunc");
    g_ime_set_enter = (FnImeSetSendEnterKey)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetSendEnterKeyFunc");
    g_ime_set_move = (FnImeSetMoveCursor)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetMoveCursorFunc");
    g_ime_set_kbstatus = (FnImeSetSendKeyboardStatus)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetSendKeyboardStatusFunc");
    g_ime_setsel = (FnImeSetHandleSetSelection)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetHandleSetSelectionFunc");
    g_ime_setext = (FnImeSetHandleExtendAction)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetHandleExtendActionFunc");
    g_ime_set_left = (FnImeSetGetLeftTextOfCursor)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetGetLeftTextOfCursorFunc");
    g_ime_set_right = (FnImeSetGetRightTextOfCursor)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetGetRightTextOfCursorFunc");
    g_ime_set_idx = (FnImeSetGetTextIndexAtCursor)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetGetTextIndexAtCursorFunc");
    g_ime_set_priv = (FnImeSetReceivePrivateCommand)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetReceivePrivateCommandFunc");
    g_ime_set_prev = (FnImeSetSetPreviewText)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetPreviewTextFunc");
    g_ime_set_finprev = (FnImeSetFinishTextPreview)dlsym(RTLD_DEFAULT, "OH_TextEditorProxy_SetFinishTextPreviewFunc");
    g_ime_attach = (FnImeAttach)dlsym(RTLD_DEFAULT, "OH_InputMethodController_Attach");
    g_ime_detach = (FnImeDetach)dlsym(RTLD_DEFAULT, "OH_InputMethodController_Detach");
    g_ime_opts_create = (FnImeAttachOptsCreate)dlsym(RTLD_DEFAULT, "OH_AttachOptions_Create");
    g_ime_opts_destroy = (FnImeAttachOptsDestroy)dlsym(RTLD_DEFAULT, "OH_AttachOptions_Destroy");
    /* 可选 TextConfig 支持：接口缺失时优雅降级 */
    g_ime_cfg_input = (FnImeCfgSetInputType)dlsym(RTLD_DEFAULT, "OH_TextConfig_SetInputType");
    g_ime_cfg_enter = (FnImeCfgSetEnterKeyType)dlsym(RTLD_DEFAULT, "OH_TextConfig_SetEnterKeyType");
    g_ime_cfg_preview = (FnImeCfgSetPreviewSupport)dlsym(RTLD_DEFAULT, "OH_TextConfig_SetPreviewTextSupport");
    if (!g_ime_proxy_destroy || !g_ime_set_cfg || !g_ime_set_insert ||
        !g_ime_set_delbwd || !g_ime_set_delfwd || !g_ime_set_enter ||
        !g_ime_set_move || !g_ime_set_left || !g_ime_set_right ||
        !g_ime_set_idx || !g_ime_set_priv || !g_ime_set_prev ||
        !g_ime_attach || !g_ime_detach || !g_ime_opts_create ||
        !g_ime_opts_destroy) {
        memset(&g_ime_proxy_create, 0, sizeof(g_ime_proxy_create));
    }
}

/* 焦点驱动打开/关闭输入法会话 */
EXPORT void zan_gui_set_ime_open(i32 on) {
    ime_feature_detect();
    ime_ensure_proxy();
    if (!g_ime_proxy) { return; }
    if (on) {
        if (g_ime_attached) { return; }
        InputMethod_AttachOptions *opts = g_ime_opts_create(true);
        if (!opts) { return; }
        InputMethod_InputMethodProxy *im = NULL;
        InputMethod_ErrorCode ec = g_ime_attach(g_ime_proxy, opts, &im);
        g_ime_opts_destroy(opts);
        if (ec == IME_ERR_OK && im) {
            g_ime_im = im;
            g_ime_attached = 1;
        }
    } else {
        if (!g_ime_attached) { return; }
        g_ime_detach(g_ime_im);
        g_ime_im = NULL;
        g_ime_attached = 0;
    }
}

void zan_gui_ohos_ime_shutdown(void) {
    if (g_ime_attached) {
        g_ime_detach(g_ime_im);
        g_ime_im = NULL;
        g_ime_attached = 0;
    }
    if (g_ime_proxy) {
        g_ime_proxy_destroy(g_ime_proxy);
        g_ime_proxy = NULL;
    }
}

/* 内部辅助实现 */
static const char *k_ohos_vs =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
    "}\n";
static const char *k_ohos_fs =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.b, c.g, c.r, c.a);\n"
    "}\n";

static GLuint ohos_compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

/* EGL 呈现环境初始化（支持窗口旋转与重连） */
static int ohos_gl_surface(zan_ohos_win_t *w) {
    if (!w->egl_dpy) {
        w->egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (w->egl_dpy == EGL_NO_DISPLAY || !eglInitialize(w->egl_dpy, NULL, NULL))
            return 1;
    }
    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = NULL;
    EGLint n = 0;
    if (!eglChooseConfig(w->egl_dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
        return 1;
    if (!w->egl_surf) {
        w->egl_surf = eglCreateWindowSurface(w->egl_dpy, cfg,
                                             (EGLNativeWindowType)w->nw, NULL);
        if (w->egl_surf == EGL_NO_SURFACE) return 1;
        w->surf_nw = w->nw;
        /* 内部辅助实现 */
        EGLint qw = 0, qh = 0;
        if (eglQuerySurface(w->egl_dpy, w->egl_surf, EGL_WIDTH, &qw)
            && eglQuerySurface(w->egl_dpy, w->egl_surf, EGL_HEIGHT, &qh)
            && qw > 0 && qh > 0) {
            w->surf_w = qw;
            w->surf_h = qh;
        } else {
            w->surf_w = w->w;
            w->surf_h = w->h;
        }
    }
    if (!w->egl_ctx) {
        const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        w->egl_ctx = eglCreateContext(w->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
        if (w->egl_ctx == EGL_NO_CONTEXT) return 1;
    }
    if (!eglMakeCurrent(w->egl_dpy, w->egl_surf, w->egl_surf, w->egl_ctx))
        return 1;
    return 0;
}

/* EGL 纹理全屏四边形着色器程序单例初始化 */
static int ohos_gl_program(zan_ohos_win_t *w) {
    if (w->gl_prog) { return 0; }

    GLuint vs = ohos_compile(GL_VERTEX_SHADER, k_ohos_vs);
    GLuint fs = ohos_compile(GL_FRAGMENT_SHADER, k_ohos_fs);
    if (!vs || !fs) return 1;
    w->gl_prog = glCreateProgram();
    glAttachShader(w->gl_prog, vs);
    glAttachShader(w->gl_prog, fs);
    glLinkProgram(w->gl_prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(w->gl_prog, GL_LINK_STATUS, &linked);
    if (!linked) return 1;

    static const GLfloat quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
    glGenBuffers(1, &w->gl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, w->gl_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(w->gl_prog, "a_pos");
    glEnableVertexAttribArray((GLuint)loc);
    glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glGenTextures(1, &w->gl_tex);
    glUseProgram(w->gl_prog);
    glUniform1i(glGetUniformLocation(w->gl_prog, "u_tex"), 0);
    return 0;
}

static int ohos_gl_init(zan_ohos_win_t *w) {
    if (ohos_gl_surface(w) != 0) { return 1; }
    return ohos_gl_program(w);
}

/* 底层系统交互与数据协议契约 */
#define ZAN_OHOS_DIRTY_MAX 512
static i32 g_dirty[ZAN_OHOS_DIRTY_MAX * 4];
static int g_dirty_count;
static int g_dirty_overflow;

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_dirty_count >= ZAN_OHOS_DIRTY_MAX) { g_dirty_overflow = 1; return 0; }
    g_dirty[g_dirty_count * 4 + 0] = x;
    g_dirty[g_dirty_count * 4 + 1] = y;
    g_dirty[g_dirty_count * 4 + 2] = w;
    g_dirty[g_dirty_count * 4 + 3] = h;
    g_dirty_count++;
    return 0;
}

static int g_dirty_full;

static void ohos_dirty_reset(void) {
    g_dirty_count = 0;
    g_dirty_overflow = 0;
    g_dirty_full = 0;
}

/* 底层系统交互与数据协议契约 */
EXPORT void zan_gui_present_full(void) {
    g_dirty_full = 1;
}

static void ohos_texture(zan_ohos_win_t *w, const zan_surface_t *s) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, w->gl_tex);
    if (w->tex_w != s->width || w->tex_h != s->height) {
        /* 窗口创建与尺寸调整：全屏刷新并包含当前帧脏矩形 */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->width, s->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        w->tex_w = s->width;
        w->tex_h = s->height;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                        GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else if (g_dirty_count > 0 && !g_dirty_overflow && !g_dirty_full) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
        for (int i = 0; i < g_dirty_count; i++) {
            i32 x = g_dirty[i * 4 + 0], y = g_dirty[i * 4 + 1];
            i32 cw = g_dirty[i * 4 + 2], ch = g_dirty[i * 4 + 3];
            if (x < 0) { cw += x; x = 0; }
            if (y < 0) { ch += y; y = 0; }
            if (x + cw > s->width) { cw = s->width - x; }
            if (y + ch > s->height) { ch = s->height - y; }
            if (cw <= 0 || ch <= 0) continue;
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, cw, ch,
                            GL_RGBA, GL_UNSIGNED_BYTE,
                            (const uint8_t *)s->pixels
                                + ((size_t)y * (size_t)s->stride + (size_t)x) * 4);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                        GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

EXPORT i32 zan_gui_present(iptr hwnd_val, i32 surface_id) {
    (void)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 1;
    if (!g_owin.attached || !g_owin.nw) return 1;
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    /* 真机加速路径：通过 NDK 直接写入 BufferQueue 像素缓冲区 */
    if (g_nw_request && g_nw_flush && g_nw_getbh) {
        OHNativeWindowBuffer *buf = NULL;
        int fence = -1;
        if (g_nw_request((OHNativeWindow *)g_owin.nw, &buf, &fence) != 0)
            goto egl_path;
        BufferHandle *bh = g_nw_getbh(buf);
        if (bh && bh->virAddr) {
            int bw = (int)bh->width;    /* 核心系统底层抽象与内存语义契约 */
            int bstride = (int)bh->stride;
            int copy_w = s->width < bw ? s->width : bw;
            int copy_h = s->height < (int)bh->height ? s->height : (int)bh->height;
            uint8_t *dst = (uint8_t *)bh->virAddr;
            const uint8_t *src = (const uint8_t *)s->pixels;
            size_t row = (size_t)copy_w * 4;
            for (int y = 0; y < copy_h; y++)
                memcpy(dst + (size_t)y * (size_t)bstride * 4,
                       src + (size_t)y * (size_t)s->stride * 4, row);
            Region region;
            region.rects = NULL;   /* 核心系统底层抽象与内存语义契约 */
            region.rectNumber = 0;
            g_nw_flush((OHNativeWindow *)g_owin.nw, buf, -1, &region);
            ohos_dirty_reset();
            return 0;
        }
    }
egl_path:
    /* 底层系统交互与数据协议契约 */
    if (g_owin.egl_surf && (g_owin.surf_nw != g_owin.nw
                            || g_owin.surf_w != g_owin.w
                            || g_owin.surf_h != g_owin.h)) {
        /* 内部辅助实现 */
        eglMakeCurrent(g_owin.egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroySurface(g_owin.egl_dpy, g_owin.egl_surf);
        g_owin.egl_surf = EGL_NO_SURFACE;
        g_owin.surf_nw = NULL;
        g_owin.surf_w = g_owin.surf_h = 0;
    }
    if ((!g_owin.gl_prog || !g_owin.egl_surf) && ohos_gl_init(&g_owin) != 0) {
        ohos_dirty_reset();
        return 1;
    }
    ohos_texture(&g_owin, s);
    ohos_dirty_reset();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(g_owin.egl_dpy, g_owin.egl_surf);
    return 0;
}

/* 底层系统交互与数据协议契约 */
EXPORT void zan_gui_ohos_shutdown(void) {
    zan_gui_ohos_ime_shutdown();
    zan_ohos_win_t *w = &g_owin;
    if (w->egl_dpy != EGL_NO_DISPLAY && w->egl_dpy) {
        eglMakeCurrent(w->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (w->egl_surf != EGL_NO_SURFACE)
            eglDestroySurface(w->egl_dpy, w->egl_surf);
        if (w->egl_ctx != EGL_NO_CONTEXT)
            eglDestroyContext(w->egl_dpy, w->egl_ctx);
        eglTerminate(w->egl_dpy);
    }
    w->egl_surf = EGL_NO_SURFACE;
    w->egl_ctx = EGL_NO_CONTEXT;
    w->egl_dpy = EGL_NO_DISPLAY;
    w->surf_nw = NULL;
    w->surf_w = w->surf_h = 0;
    w->gl_prog = 0;
    w->tex_w = w->tex_h = 0;
}

/* 核心系统底层抽象与内存语义契约 */

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title;
    (void)width; (void)height; /* 底层系统交互与数据协议契约 */
    return ZAN_OHOS_HWND;
}
EXPORT i32 zan_gui_show_window(iptr hwnd_val)            { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_minimize(iptr hwnd_val)               { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_toggle_maximize(iptr hwnd_val)        { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_close_window(iptr hwnd_val) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_oq_lock);
    g_owin.closed = 1;
    oq_push_locked(8, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);
    return 0;
}
EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_oq_lock);
    g_owin.attached = 0;
    g_owin.nw = NULL;
    pthread_mutex_unlock(&g_oq_lock);
    return 0;
}
EXPORT i32 zan_gui_is_maximized(iptr hwnd_val)     { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_window_visible(iptr hwnd_val)   { (void)hwnd_val; return g_owin.attached; }
EXPORT i32 zan_gui_window_focused(iptr hwnd_val)   { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_titlebar_height(void)           { return 0; }
EXPORT i32 zan_gui_caption_button_width(void)      { return 0; }
EXPORT i32 zan_gui_set_caption_buttons(iptr h, i32 n) { (void)h; (void)n; return 0; }
EXPORT i32 zan_gui_set_window_pos(iptr h, i32 x, i32 y) { (void)h; (void)x; (void)y; return 0; }
EXPORT i32 zan_gui_center_window(iptr hwnd_val)    { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_set_topmost(iptr h, i32 on)     { (void)h; (void)on; return 0; }
EXPORT i32 zan_gui_set_title(iptr h, const char *t) { (void)h; (void)t; return 0; }
EXPORT i32 zan_gui_set_cursor(i32 cursor_type)     { (void)cursor_type; return 0; }

/* 核心系统底层抽象与内存语义契约 */

static int oq_pop(void) {
    pthread_mutex_lock(&g_oq_lock);
    if (g_oq_head == g_oq_tail) { pthread_mutex_unlock(&g_oq_lock); return 0; }
    zan_oev_t *z = &g_oq[g_oq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_oq_head = (g_oq_head + 1) % ZAN_OQ_CAP;
    g_ev_seq++;
    pthread_mutex_unlock(&g_oq_lock);
    return 1;
}

/* 内部辅助实现 */
EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (oq_pop()) { return 0; }
    return 1;
}

EXPORT i32 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    while (!oq_pop()) {
        struct timespec ts = { 0, 4 * 1000 * 1000 }; /* 4ms */
        nanosleep(&ts, NULL);
    }
    return 0;
}

EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (oq_pop()) { return 0; }
    int remain = (ms < 0) ? 0 : ms;
    while (remain > 0) {
        long step = (remain > 8) ? 8 : (long)remain;
        struct timespec ts = { 0, step * 1000 * 1000 };
        nanosleep(&ts, NULL);
        remain -= (int)step;
        if (oq_pop()) { return 0; }
    }
    return 1;
}

EXPORT i32 zan_gui_wake(void) { return 0; }
/* 内部辅助实现 */
EXPORT i32 zan_gui_inject_event(
    iptr hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_oq_lock);
    oq_push_locked((int)kind, (int)x, (int)y, (int)button, (int)keycode,
                   (int)mods);
    pthread_mutex_unlock(&g_oq_lock);
    return 0;
}
EXPORT i32 zan_gui_inject_pending(void) {
    return (g_oq_tail - g_oq_head + ZAN_OQ_CAP) % ZAN_OQ_CAP;
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

EXPORT i32 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i32 zan_gui_window_height(void) { return g_window_height; }
EXPORT i32 zan_gui_client_width(iptr hwnd_val)  { (void)hwnd_val; return g_window_width; }
EXPORT i32 zan_gui_client_height(iptr hwnd_val) { (void)hwnd_val; return g_window_height; }

/* 核心系统底层抽象与内存语义契约 */

EXPORT i32 zan_gui_get_dpi_scale(void) { return (i32)(g_dpi * 100 / 96); }

/* 内部辅助实现 */
EXPORT void zan_gui_ohos_set_dpi(i32 dpi) {
    if (dpi >= 48 && dpi <= 960) g_dpi = (int)((long)dpi * 96 / 160);
}

EXPORT i64 zan_gui_get_tick_ms(void) { return ohs_tick_ms(); }
EXPORT void zan_gui_sleep_ms(i32 ms) {
    if (ms > 0) {
        struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

static char g_files_dir[256] = "/data/local/tmp";
EXPORT const char *zan_gui_android_files_dir(void) { return g_files_dir; }
EXPORT void zan_gui_ohos_set_files_dir(const char *path) {
    if (!path) return;
    snprintf(g_files_dir, sizeof(g_files_dir), "%s", path);
}

EXPORT i32 zan_gui_set_clipboard(const char *utf8) { (void)utf8; return 1; }
EXPORT const char *zan_gui_get_clipboard(void)     { return ""; }
EXPORT int  zan_gui_drop_pending(void)             { return 0; }
EXPORT const char *zan_gui_drop_take(void)         { return ""; }
EXPORT void zan_gui_set_ime_pos(i32 x, i32 y)      { (void)x; (void)y; }
/* 底层系统交互与数据协议契约 */
EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1; /* 核心系统底层抽象与内存语义契约 */
}
EXPORT i32 zan_gui_disable_glass(iptr hwnd_val)    { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_set_opacity(iptr h, i32 percent) { (void)h; (void)percent; return 0; }

EXPORT i32 zan_gui_write_file(const char *path, const char *utf8) {
    if (!path || !utf8) return 1;
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t n = strlen(utf8);
    size_t w = fwrite(utf8, 1, n, f);
    fclose(f);
    return w == n ? 0 : 1;
}

/* 底层系统交互与数据协议契约 */
EXPORT i32 zan_gui_adopt_sdl_window(iptr hwnd_val)                  { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_scene_set_renderer(iptr hwnd_val, iptr rend)     { (void)hwnd_val; (void)rend; return 1; }
EXPORT i32 zan_gui_scene_upload(iptr hwnd_val, const void *bgra, i32 w, i32 h) {
    (void)hwnd_val; (void)bgra; (void)w; (void)h; return 1;
}
EXPORT i32 zan_gui_scene_present(iptr hwnd_val, i32 surface_id) {
    (void)hwnd_val; (void)surface_id; return 1;
}

#endif /* ZAN_GUI_OHOS */
