/* gui_runtime_android_native */

/* The native clock regression includes this production block without Android SDK headers. */
#if defined(ZAN_GUI_ANDROID_NATIVE) || defined(ZAN_GUI_ANDROID_CLOCK_TEST)
#ifdef ZAN_GUI_ANDROID_NATIVE
#include <pthread.h>
#endif
static pthread_once_t g_tick_once = PTHREAD_ONCE_INIT;
static i64 g_tick_origin_ms;

static void ant_tick_init(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_tick_origin_ms = (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static i64 ant_tick_ms(void) {
    /* Window.GetTickMs narrows to int: use driver lifetime, not device uptime.
       Initialize before sampling now so concurrent first callers cannot go negative.
       This origin survives Activity/Surface recreation and resume. */
    pthread_once(&g_tick_once, ant_tick_init);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 - g_tick_origin_ms;
}
#endif

#ifdef ZAN_GUI_ANDROID_NATIVE

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android/choreographer.h>
#include <android/configuration.h>
#include <android/window.h>
#include <android/log.h>

#include "android_native_app_glue.h"
#include "gui_touch_game.h"

static void zan_alog(const char *fmt, ...) __attribute__((format(printf,1,2)));
static void zan_alog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, "zanShell", fmt, ap);
    va_end(ap);
}
#define ZAN_LOG(...) zan_alog(__VA_ARGS__)
#ifdef ZAN_SHELL_TRACE
#define ZAN_TRACE(...) ZAN_LOG(__VA_ARGS__)
#else
#define ZAN_TRACE(...) ((void)0)
#endif

typedef struct {
    ANativeWindow *nw;
    int w, h;
    int attached;
    int closed;

    /* EGL 呈现状态：在 surface 附着或首次 present 时创建（运行于 glue 应用线程） */
    EGLDisplay egl_dpy;
    EGLSurface egl_surf;
    EGLContext egl_ctx;
    ANativeWindow *surf_nw;
    int        surf_w, surf_h;
    GLuint     gl_prog;
    GLuint     gl_tex;
    GLuint     gl_vbo;
    int        tex_w, tex_h;

    struct android_app *app;
    int destroy_wait;
} zan_anw_t;

static zan_anw_t g_anw;
#define ZAN_ANW_HWND ((iptr)&g_anw)

static int  g_window_width  = 0;
static int  g_window_height = 0;
static int  g_dpi           = 96;

static int  g_safe_area_top    = 0;
static int  g_safe_area_bottom = 0;
static int  g_safe_area_left   = 0;
static int  g_safe_area_right  = 0;

EXPORT void zan_gui_android_set_safe_area(i32 top, i32 bottom, i32 left, i32 right) {
    if (top >= 0) g_safe_area_top = (int)top;
    if (bottom >= 0) g_safe_area_bottom = (int)bottom;
    if (left >= 0) g_safe_area_left = (int)left;
    if (right >= 0) g_safe_area_right = (int)right;
}

static int  g_keyboard_height  = 0;

EXPORT void zan_gui_android_set_keyboard_height(i32 height) {
    if (height >= 0) g_keyboard_height = (int)height;
}

typedef struct { int e[8]; iptr win; } zan_aev_t;
#define ZAN_AQ_CAP 512
static zan_aev_t g_aq[ZAN_AQ_CAP];
static int g_aq_head = 0, g_aq_tail = 0;
static pthread_mutex_t g_aq_lock = PTHREAD_MUTEX_INITIALIZER;
static ALooper *g_ui_looper;
static uint64_t g_wake_seq, g_seen_wake_seq;
static AChoreographer *g_display_choreographer;
static uintptr_t g_display_frame_serial, g_display_frame_pending;
static int g_display_frame_ready;

/* API24 回调不可取消；失效请求的 token 不能清除下一窗口的新请求。 */
static void ant_invalidate_display_frame_locked(void) {
    g_display_frame_pending = 0;
    g_display_frame_ready = 0;
}
static double g_refresh_hz = 60.0;
static i64 g_refresh_checked_ms;
static int g_refresh_valid;

/* 队列锁同时保护 Looper 的持有期；poll 的任何返回都可能合并唤醒。 */
static void ant_wake_locked(void) {
    if (g_ui_looper) {
        ++g_wake_seq;
        ALooper_wake(g_ui_looper);
    }
}

static int ant_take_wake(void) {
    pthread_mutex_lock(&g_aq_lock);
    /* Poll/Wait must keep reporting an unconsumed frame after its wake is taken. */
    int woke = g_seen_wake_seq != g_wake_seq || g_display_frame_ready;
    g_seen_wake_seq = g_wake_seq;
    pthread_mutex_unlock(&g_aq_lock);
    return woke;
}

static int g_pending_event[8];
static iptr g_event_win = 0;
static long long g_ev_seq = 0;

/* 事件标志位属于事件记录的一部分，入队时一并保存以防溢出丢失 */
static void aq_push_event_locked(const int event[8]) {
    int last = (g_aq_tail + ZAN_AQ_CAP - 1) % ZAN_AQ_CAP;
    if (g_aq_head != g_aq_tail && zan_tg_coalesce(g_aq[last].e, event)) return;
    int next = (g_aq_tail + 1) % ZAN_AQ_CAP;
    if (next == g_aq_head) return; /* full: drop */
    zan_aev_t *z = &g_aq[g_aq_tail];
    memcpy(z->e, event, sizeof(z->e));
    z->win = ZAN_ANW_HWND;
    g_aq_tail = next;
    ant_wake_locked();
}

static void aq_push_locked(int kind, int x, int y, int button, int code, int mods) {
    const int event[8] = { kind, x, y, button, code, mods, 0, 0 };
    aq_push_event_locked(event);
}

static void aq_push_flag_locked(int kind, int x, int y, int button, int code, int mods) {
    const int event[8] = { kind, x, y, button, code, mods, 1, 0 };
    aq_push_event_locked(event);
}

static int aq_pop(void) {
    pthread_mutex_lock(&g_aq_lock);
    if (g_aq_head == g_aq_tail) { pthread_mutex_unlock(&g_aq_lock); return 0; }
    zan_aev_t *z = &g_aq[g_aq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_aq_head = (g_aq_head + 1) % ZAN_AQ_CAP;
    g_ev_seq++;
    pthread_mutex_unlock(&g_aq_lock);
    return 1;
}


static void ant_gl_reset(void);

static void anw_attach(ANativeWindow *nw) {
    int w = ANativeWindow_getWidth(nw);
    int h = ANativeWindow_getHeight(nw);
    pthread_mutex_lock(&g_aq_lock);
    g_anw.nw = nw;
    g_anw.w = w; g_anw.h = h;
    g_anw.attached = 1;
    g_refresh_valid = 0;
    ant_invalidate_display_frame_locked();
    g_window_width = w;
    g_window_height = h;
    aq_push_locked(7, w, h, 0, 0, 0);
    /* Surface 重新附着：强制触发全屏重绘并唤醒 WaitEvent 挂起线程 */
    aq_push_locked(14, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_aq_lock);
    /* 新 ANativeWindow 初始化：重置前一窗口残留的 GL 脏矩形状态 */
    ant_gl_reset();
}

static void anw_detach(void) {
    pthread_mutex_lock(&g_aq_lock);
    g_anw.attached = 0;
    g_anw.nw = NULL;
    ant_invalidate_display_frame_locked();
    pthread_mutex_unlock(&g_aq_lock);
}

#define ANT_TOUCH_SLOP2   64.0f
#define ANT_TOUCH_HIST    8
#define ANT_FLING_START_PX_S 250.0f
#define ANT_FLING_STOP_PX_S  120.0f
#define ANT_FLING_TAU_MS     400.0f

static int   g_tg_down, g_tg_drag;
static float g_tg_ax, g_tg_ay;
static float g_tg_x, g_tg_y;
static float g_tg_acc;
static long long g_tg_ht[ANT_TOUCH_HIST];
static float g_tg_hy[ANT_TOUCH_HIST];
static int   g_tg_hn, g_tg_hi;

static int   g_fling_active;
static float g_fling_v;
static float g_fling_acc;
static int   g_fling_x, g_fling_y;
static long long g_fling_last;
static pthread_t g_fling_thr;
static int g_fling_thr_up;

static void *fling_thread(void *arg) {
    (void)arg;
    for (;;) {
        int live;
        pthread_mutex_lock(&g_aq_lock);
        live = g_fling_active;
        if (live) {
            long long now = ant_tick_ms();
            float dt = (float)(now - g_fling_last) / 1000.0f;
            g_fling_last = now;
            if (dt > 0) {
                g_fling_v *= expf(-dt * 1000.0f / ANT_FLING_TAU_MS);
                g_fling_acc += g_fling_v * dt * 288.0f / (float)g_dpi;
                int delta = (int)g_fling_acc;
                if (delta != 0) {
                    g_fling_acc -= (float)delta;
                    aq_push_locked(13, g_fling_x, g_fling_y, 0, delta, 0);
                }
            }
            if (fabsf(g_fling_v) < ANT_FLING_STOP_PX_S) g_fling_active = 0;
        }
        pthread_mutex_unlock(&g_aq_lock);
        if (!live) break;
        struct timespec ts = { 0, 16 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    g_fling_thr_up = 0;
    return NULL;
}

static void fling_cancel_locked(void) { g_fling_active = 0; }

static void fling_start(float v, int x, int y) {
    if (fabsf(v) < ANT_FLING_START_PX_S) return;
    pthread_mutex_lock(&g_aq_lock);
    g_fling_v = v;
    g_fling_acc = 0;
    g_fling_x = x; g_fling_y = y;
    g_fling_last = ant_tick_ms();
    g_fling_active = 1;
    int need = !g_fling_thr_up;
    g_fling_thr_up = 1;
    pthread_mutex_unlock(&g_aq_lock);
    if (need && pthread_create(&g_fling_thr, NULL, fling_thread, NULL) != 0)
        g_fling_thr_up = 0;
}

/* 默认启用传统 GUI 滚动，游戏场景按需接入原始拖拽与捏合手势 */
static void ant_touch(int action, int x, int y) {
    static int px = 0, py = 0;
    if (x < 0 && y < 0) { x = px; y = py; }
    px = x; py = y;

    if (action == 0) {
        pthread_mutex_lock(&g_aq_lock);
        fling_cancel_locked();
        g_tg_down = 1; g_tg_drag = 0; g_tg_acc = 0;
        g_tg_x = g_tg_ax = (float)x;
        g_tg_y = g_tg_ay = (float)y;
        g_tg_hn = 1; g_tg_hi = 1 % ANT_TOUCH_HIST;
        g_tg_ht[0] = ant_tick_ms(); g_tg_hy[0] = g_tg_y;
        /* 按下自带坐标，App 直接命中目标；不额外生成触屏悬停帧。 */
        aq_push_locked(2, (int)g_tg_ax, (int)g_tg_ay, 0, 0, 0);
        pthread_mutex_unlock(&g_aq_lock);
        return;
    }
    if (action == 1 && g_tg_down) {
        pthread_mutex_lock(&g_aq_lock);
        float fx = (float)x, fy = (float)y;
        if (!g_tg_drag) {
            float dx = fx - g_tg_ax, dy = fy - g_tg_ay;
            if (dx * dx + dy * dy < ANT_TOUCH_SLOP2) {
                pthread_mutex_unlock(&g_aq_lock);
                return;
            }
            g_tg_drag = 1;
        }
        /* 手指位移映射为标准滚轮步长 (/120 比例)：方向与手指拖拽自然反向 */
        g_tg_acc += (fy - g_tg_y) * 288.0f / (float)g_dpi;
        int delta = (int)g_tg_acc;
        if (delta != 0) {
            g_tg_acc -= (float)delta;
            aq_push_locked(13, x, y, 0, delta, 0);
        }
        g_tg_ht[g_tg_hi] = ant_tick_ms(); g_tg_hy[g_tg_hi] = fy;
        g_tg_hi = (g_tg_hi + 1) % ANT_TOUCH_HIST;
        if (g_tg_hn < ANT_TOUCH_HIST) g_tg_hn++;
        g_tg_x = fx; g_tg_y = fy;
        if (delta == 0) aq_push_flag_locked(1, x, y, 0, 0, 0);
        pthread_mutex_unlock(&g_aq_lock);
        return;
    }
    if (action == 2 && g_tg_down) {
        g_tg_down = 0;
        pthread_mutex_lock(&g_aq_lock);
        if (g_tg_drag) {
            aq_push_flag_locked(3, (int)g_tg_x, (int)g_tg_y, 0, 0, 0);
            pthread_mutex_unlock(&g_aq_lock);
            int last = (g_tg_hi + ANT_TOUCH_HIST - 1) % ANT_TOUCH_HIST;
            int old = last;
            for (int k = 0; k < g_tg_hn; k++) {
                int idx = (last - k + ANT_TOUCH_HIST) % ANT_TOUCH_HIST;
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
            int ax = (int)g_tg_ax, ay = (int)g_tg_ay;
            aq_push_locked(3, ax, ay, 0, 0, 0);
            pthread_mutex_unlock(&g_aq_lock);
        }
        return;
    }
}

static int g_touch_game_mode = -1;
static zan_touch_game g_touch_game;

static int ant_touch_game_enabled(void) {
    if (g_touch_game_mode < 0) {
        const char *value = getenv("ZAN_GUI_TOUCH_GAME");
        g_touch_game_mode = value && strcmp(value, "1") == 0;
    }
    return g_touch_game_mode;
}

static void ant_touch_game_emit(void *ctx, const int event[8]) {
    (void)ctx;
    aq_push_event_locked(event);
}

static void ant_touch_game_cancel_locked(void) {
    if (g_touch_game_mode == 1)
        zan_touch_game_feed(&g_touch_game, ZAN_TG_CANCEL, -1, NULL, 0,
                           ant_touch_game_emit, NULL);
}

static void ant_touch_game_input(AInputEvent *ev, int raw_action) {
    int action;
    switch (raw_action & AMOTION_EVENT_ACTION_MASK) {
    case AMOTION_EVENT_ACTION_DOWN: action = ZAN_TG_DOWN; break;
    case AMOTION_EVENT_ACTION_MOVE: action = ZAN_TG_MOVE; break;
    case AMOTION_EVENT_ACTION_UP: action = ZAN_TG_UP; break;
    case AMOTION_EVENT_ACTION_CANCEL: action = ZAN_TG_CANCEL; break;
    case AMOTION_EVENT_ACTION_POINTER_DOWN: action = ZAN_TG_POINTER_DOWN; break;
    case AMOTION_EVENT_ACTION_POINTER_UP: action = ZAN_TG_POINTER_UP; break;
    default: return;
    }
    /* Android 触控点 ID 映射 (0..31)：维护多指触控状态机 */
    zan_tg_pointer pointers[32];
    size_t count = AMotionEvent_getPointerCount(ev);
    size_t changed = (size_t)((raw_action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    int changed_id = -1;
    if (action == ZAN_TG_CANCEL || count == 0 || count > 32 || changed >= count) {
        action = ZAN_TG_CANCEL;
        count = 0;
    } else {
        for (size_t i = 0; i < count; ++i) {
            pointers[i].id = AMotionEvent_getPointerId(ev, i);
            pointers[i].x = AMotionEvent_getX(ev, i);
            pointers[i].y = AMotionEvent_getY(ev, i);
        }
        changed_id = pointers[changed].id;
    }
    pthread_mutex_lock(&g_aq_lock);
    fling_cancel_locked();
    zan_touch_game_feed(&g_touch_game, action, changed_id, pointers, (int)count,
                       ant_touch_game_emit, NULL);
    pthread_mutex_unlock(&g_aq_lock);
}

/* AInputEvent 转换为环形队列事件，消费成功返回 1 */
static int32_t ant_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        int raw_action = AMotionEvent_getAction(ev);
        if (ant_touch_game_enabled()) {
            ant_touch_game_input(ev, raw_action);
            return 1;
        }
        int action = raw_action & AMOTION_EVENT_ACTION_MASK;
        float fx = AMotionEvent_getX(ev, 0);
        float fy = AMotionEvent_getY(ev, 0);
        int x = (int)fx, y = (int)fy;
        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            ant_touch(0, x, y);
            return 1;
        case AMOTION_EVENT_ACTION_MOVE:
            ant_touch(1, x, y);
            return 1;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            ant_touch(2, x, y);
            return 1;
        default:
            /* 多指触控转换：持续维护主触控点坐标，保证拖拽手势连续性 */
            if (g_tg_down) ant_touch(1, x, y);
            return 1;
        }
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        int32_t kc = AKeyEvent_getKeyCode(ev);
        int32_t act = AKeyEvent_getAction(ev);
        if (kc == AKEYCODE_BACK) {
            const char *back_as_escape = getenv("ZAN_GUI_BACK_AS_ESCAPE");
            pthread_mutex_lock(&g_aq_lock);
            if (back_as_escape && strcmp(back_as_escape, "1") == 0) {
                if (act == AKEY_EVENT_ACTION_UP ||
                    (act == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(ev) == 0)) {
                    aq_push_locked(act == AKEY_EVENT_ACTION_DOWN ? 4 : 5,
                                   0, 0, 0, 27, 0);
                }
            } else if (act == AKEY_EVENT_ACTION_DOWN) {
                g_anw.closed = 1;
                aq_push_locked(8, 0, 0, 0, 0, 0);
            }
            pthread_mutex_unlock(&g_aq_lock);
            return 1;
        }
        if (act != AKEY_EVENT_ACTION_DOWN) return 1;
        if (kc == AKEYCODE_DEL || kc == AKEYCODE_FORWARD_DEL) {
            pthread_mutex_lock(&g_aq_lock);
            aq_push_locked(6, 0, 0, 0, 8, 0);
            pthread_mutex_unlock(&g_aq_lock);
            return 1;
        }
        if (kc == AKEYCODE_ENTER || kc == AKEYCODE_NUMPAD_ENTER) {
            pthread_mutex_lock(&g_aq_lock);
            aq_push_locked(6, 0, 0, 0, 13, 0);
            pthread_mutex_unlock(&g_aq_lock);
            return 1;
        }
        /* 软键盘可见 ASCII 按键事件转发映射 */
        if (kc >= AKEYCODE_SPACE && kc <= AKEYCODE_Z) {
            static const char *map =
                " ??##  abcdefghijklmnop   0123456789  ";
            /* AKEYCODE_SPACE=62, digits 7..16, letters 29..54 */
            (void)map;
            int ch = 0;
            if (kc == AKEYCODE_SPACE) ch = ' ';
            else if (kc >= AKEYCODE_0 && kc <= AKEYCODE_9)
                ch = '0' + (kc - AKEYCODE_0);
            else if (kc >= AKEYCODE_A && kc <= AKEYCODE_Z)
                ch = 'a' + (kc - AKEYCODE_A);
            if (ch) {
                pthread_mutex_lock(&g_aq_lock);
                aq_push_locked(6, 0, 0, 0, ch, 0);
                pthread_mutex_unlock(&g_aq_lock);
                return 1;
            }
        }
        return 1;
    }
    return 0;
}

static void fling_cancel_public(void);

static int ant_poll_looper(int timeout_ms);
static void ant_pump_looper(void);

static void ant_set_immersive(void);

static void ant_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        pthread_mutex_lock(&g_aq_lock);
        g_anw.destroy_wait = 0;
        pthread_mutex_unlock(&g_aq_lock);
        ant_set_immersive();
        anw_attach(app->window);
        break;
    case APP_CMD_TERM_WINDOW:
        /* Surface 丢失前取消当前进行中的手势，避免状态挂起 */
        if (g_touch_game_mode == 1) fling_cancel_public();
        anw_detach();
        break;
    case APP_CMD_WINDOW_RESIZED:
        if (app->window) {
            int w = ANativeWindow_getWidth(app->window);
            int h = ANativeWindow_getHeight(app->window);
            pthread_mutex_lock(&g_aq_lock);
            g_anw.w = w; g_anw.h = h;
            g_window_width = w; g_window_height = h;
            aq_push_locked(7, w, h, 0, 0, 0);
            aq_push_locked(14, 0, 0, 0, 0, 0);
            pthread_mutex_unlock(&g_aq_lock);
            /* 原地窗口尺寸变更：重新创建 EGL 表面以适配新几何尺寸 */
            g_anw.surf_w = 0;
            g_anw.surf_h = 0;
        }
        break;
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        if (g_anw.attached) {
            pthread_mutex_lock(&g_aq_lock);
            aq_push_locked(14, 0, 0, 0, 0, 0);
            pthread_mutex_unlock(&g_aq_lock);
        }
        break;
    case APP_CMD_RESUME:
        g_refresh_valid = 0;
        break;
    case APP_CMD_GAINED_FOCUS:
        /* Activity 恢复时重新隐藏系统状态栏与导航栏 */
        ant_set_immersive();
        pthread_mutex_lock(&g_aq_lock);
        g_refresh_valid = 0;
        aq_push_locked(14, 0, 0, 0, 0, 0);
        pthread_mutex_unlock(&g_aq_lock);
        break;
    case APP_CMD_LOST_FOCUS:
        fling_cancel_public();
        break;
    default:
        break;
    }
}

static void fling_cancel_public(void) {
    pthread_mutex_lock(&g_aq_lock);
    fling_cancel_locked();
    ant_touch_game_cancel_locked();
    pthread_mutex_unlock(&g_aq_lock);
}


static char g_files_dir[512] = "/data/local/tmp";

static void ant_set_dpi(struct android_app *app) {
    if (!app->config) return;
    int32_t d = AConfiguration_getDensity(app->config);
    /* 屏幕 DPI 缺省值回退：以 160 DPI 为基准刻度 */
    if (d > 0 && d < 0xFF00) {
        g_dpi = (int)((long)d * 96 / 160);
    }
}

int main(int argc, char **argv);

void android_main(struct android_app *app) {
    app->onAppCmd = ant_cmd;
    pthread_mutex_lock(&g_aq_lock);
    g_anw.app = app;
    g_anw.closed = 0;
    g_anw.destroy_wait = 0;
    g_aq_head = g_aq_tail = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    g_ui_looper = app->looper;
    if (g_ui_looper) ALooper_acquire(g_ui_looper);
    pthread_mutex_unlock(&g_aq_lock);

    ant_set_dpi(app);

    const char *files = app->activity ? app->activity->internalDataPath : NULL;
    if (files && files[0]) {
        snprintf(g_files_dir, sizeof(g_files_dir), "%s", files);
        if (chdir(g_files_dir) != 0) {
        }
    }

    /* 保持屏幕常亮 (FLAG_KEEP_SCREEN_ON) 标志位设置 */
    if (app->activity) {
        ANativeActivity_setWindowFlags(app->activity,
            AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    }

    /* 沉浸式全屏：隐藏系统栏 */
    ant_set_immersive();

    /* 快速启动：Surface 已就绪时立即附着，无需等待 INIT_WINDOW 入队 */
    if (app->window) anw_attach(app->window);
    else {
        /* Surface 未创建：等待并回放 Activity 排队的 INIT_WINDOW 事件 */
        ant_pump_looper();
        if (!app->window) {
            pthread_mutex_lock(&g_aq_lock);
            g_anw.destroy_wait = 1;
            pthread_mutex_unlock(&g_aq_lock);
        }
    }

    /* 主循环前置生命周期处理：保留 Zan Main 配置触控模式的时机 */
    app->onInputEvent = ant_input;
    char *arg0 = "zan";
    char *argv[1] = { arg0 };
    main(1, argv);
    /* Native glue must process DESTROY before its worker returns. */
    app->onInputEvent = NULL;
    if (!app->destroyRequested) ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) {
        if (ant_poll_looper(-1) == ALOOPER_POLL_ERROR) break;
    }
    pthread_mutex_lock(&g_aq_lock);
    ALooper *looper = g_ui_looper;
    ant_invalidate_display_frame_locked();
    g_display_choreographer = NULL;
    g_ui_looper = NULL;
    g_anw.app = NULL;
    pthread_mutex_unlock(&g_aq_lock);
    if (looper) ALooper_release(looper);
}

/* ---- present (EGL) ------------------------------------------------------ */

static void ant_gl_reset(void);

static void ant_dirty_reset(void);

static const char *k_ant_vs =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
    "}\n";
static const char *k_ant_fs =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.b, c.g, c.r, c.a);\n"
    "}\n";

static GLuint ant_compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

static int ant_gl_surface(zan_anw_t *w) {
    if (!w->nw) return 1;
    if (!w->egl_dpy) {
        w->egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (w->egl_dpy == EGL_NO_DISPLAY || !eglInitialize(w->egl_dpy, NULL, NULL))
            return 1;
    }
    const EGLint cfg_attrs_v3[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, 0x00000040 /* EGL_OPENGL_ES3_BIT_KHR */,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = NULL;
    EGLint n = 0;
    if (!eglChooseConfig(w->egl_dpy, cfg_attrs_v3, &cfg, 1, &n) || n < 1) {
        const EGLint cfg_attrs_v2[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        if (!eglChooseConfig(w->egl_dpy, cfg_attrs_v2, &cfg, 1, &n) || n < 1)
            return 1;
    }
    if (!w->egl_surf || w->nw != w->surf_nw
        || w->w != w->surf_w || w->h != w->surf_h) {
        if (w->egl_surf) {
            eglMakeCurrent(w->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
            eglDestroySurface(w->egl_dpy, w->egl_surf);
            w->egl_surf = EGL_NO_SURFACE;
        }
        w->egl_surf = eglCreateWindowSurface(w->egl_dpy, cfg,
                                             (EGLNativeWindowType)w->nw, NULL);
        if (w->egl_surf == EGL_NO_SURFACE) return 1;
        w->surf_nw = w->nw;
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
        ant_gl_reset();
    }
    if (!w->egl_ctx) {
        const EGLint ctx_attrs_v3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        w->egl_ctx = eglCreateContext(w->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs_v3);
        if (w->egl_ctx == EGL_NO_CONTEXT) {
            const EGLint ctx_attrs_v2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
            w->egl_ctx = eglCreateContext(w->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs_v2);
        }
        if (w->egl_ctx == EGL_NO_CONTEXT) return 1;
    }
    if (!eglMakeCurrent(w->egl_dpy, w->egl_surf, w->egl_surf, w->egl_ctx))
        return 1;
    return 0;
}

static void ant_gl_reset(void) {
    zan_anw_t *w = &g_anw;
    w->gl_prog = 0;
    w->gl_tex = 0;
    w->gl_vbo = 0;
    w->tex_w = 0;
    w->tex_h = 0;
    ant_dirty_reset();
}

static int ant_gl_program(zan_anw_t *w) {
    if (w->gl_prog) { return 0; }
    GLuint vs = ant_compile(GL_VERTEX_SHADER, k_ant_vs);
    GLuint fs = ant_compile(GL_FRAGMENT_SHADER, k_ant_fs);
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

#define ZAN_ANT_DIRTY_MAX 512
static i32 g_dirty[ZAN_ANT_DIRTY_MAX * 4];
static int g_dirty_count;
static int g_dirty_overflow;
static int g_dirty_full;

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_dirty_count >= ZAN_ANT_DIRTY_MAX) { g_dirty_overflow = 1; return 0; }
    g_dirty[g_dirty_count * 4 + 0] = x;
    g_dirty[g_dirty_count * 4 + 1] = y;
    g_dirty[g_dirty_count * 4 + 2] = w;
    g_dirty[g_dirty_count * 4 + 3] = h;
    g_dirty_count++;
    return 0;
}

/* 全窗口呈现通知（对齐 Win32Shell.PresentFull） */
EXPORT void zan_gui_present_full(void) {
    g_dirty_full = 1;
}

static void ant_dirty_reset(void) {
    g_dirty_count = 0;
    g_dirty_overflow = 0;
    g_dirty_full = 0;
}

static void ant_texture(zan_anw_t *w, const zan_surface_t *s) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, w->gl_tex);
    if (w->tex_w != s->width || w->tex_h != s->height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->width, s->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        w->tex_w = s->width;
        w->tex_h = s->height;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                        GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else if (g_dirty_full || g_dirty_overflow) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                        GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else if (g_dirty_count > 0) {
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
    if (!g_anw.attached || !g_anw.nw || g_anw.closed) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    if (ant_gl_surface(&g_anw) != 0) {
        ant_dirty_reset(); return 1;
    }
    if (ant_gl_program(&g_anw) != 0) {
        ant_dirty_reset(); return 1;
    }
    ant_texture(&g_anw, s);
    ant_dirty_reset();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(g_anw.egl_dpy, g_anw.egl_surf);
    return 0;
}


/* JNI: Activity */
static void ant_set_orientation(int landscape) {
    struct android_app *app = g_anw.app;
    if (!app || !app->activity || !app->activity->vm) return;
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK &&
        (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
    jobject act = (jobject)app->activity->clazz;
    if (!env || !act) return;
    jclass cls = (*env)->GetObjectClass(env, act);
    if (!cls) return;
    jmethodID mid = (*env)->GetMethodID(env, cls,
        "setRequestedOrientation", "(I)V");
    if (mid) (*env)->CallVoidMethod(env, act, mid, landscape ? 0 : 1);
    (*env)->DeleteLocalRef(env, cls);
}

/* JNI: Window */
static void ant_set_immersive(void) {
    struct android_app *app = g_anw.app;
    if (!app || !app->activity || !app->activity->vm) return;
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK &&
        (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
    jobject act = (jobject)app->activity->clazz;
    if (!env || !act) return;
    jclass cls = (*env)->GetObjectClass(env, act);
    if (!cls) return;
    jmethodID getwin = (*env)->GetMethodID(env, cls, "getWindow",
                                           "()Landroid/view/Window;");
    if (!getwin) { (*env)->DeleteLocalRef(env, cls); return; }
    jobject win = (*env)->CallObjectMethod(env, act, getwin);
    if (!win) { (*env)->DeleteLocalRef(env, cls); return; }
    jclass wcls = (*env)->GetObjectClass(env, win);
    /* Android 11 (API 30+) 边缘沉浸适配 (WindowCompat.setDecorFitsSystemWindows) */
    jmethodID setfits = (*env)->GetMethodID(env, wcls,
        "setDecorFitsSystemWindows", "(Z)V");
    if (setfits) (*env)->CallVoidMethod(env, win, setfits, JNI_FALSE);
    /* 刘海屏全屏绘制支持 (LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES) */
    jmethodID getattrs = (*env)->GetMethodID(env, wcls, "getAttributes",
        "()Landroid/view/WindowManager$LayoutParams;");
    if (getattrs) {
        jobject lp = (*env)->CallObjectMethod(env, win, getattrs);
        if (lp) {
            jclass lcls = (*env)->GetObjectClass(env, lp);
            jfieldID fmode = (*env)->GetStaticFieldID(env, lcls,
                "LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS", "I");
            if (fmode) {
                jint mode = (*env)->GetStaticIntField(env, lcls, fmode);
                jfieldID flp = (*env)->GetFieldID(env, lcls,
                    "layoutInDisplayCutoutMode", "I");
                if (flp) (*env)->SetIntField(env, lp, flp, mode);
            }
            (*env)->DeleteLocalRef(env, lcls);
            (*env)->DeleteLocalRef(env, lp);
        }
    }
    jmethodID getdecor = (*env)->GetMethodID(env, wcls, "getDecorView",
                                             "()Landroid/view/View;");
    if (!getdecor) {
        (*env)->DeleteLocalRef(env, wcls);
        (*env)->DeleteLocalRef(env, win);
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    jobject decor = (*env)->CallObjectMethod(env, win, getdecor);
    if (decor) {
        jclass vcls = (*env)->GetObjectClass(env, decor);
        jmethodID setui = (*env)->GetMethodID(env, vcls,
            "setSystemUiVisibility", "(I)V");
        if (setui) (*env)->CallVoidMethod(env, decor, setui, 0x1806);
        (*env)->DeleteLocalRef(env, vcls);
        (*env)->DeleteLocalRef(env, decor);
    }
    (*env)->DeleteLocalRef(env, wcls);
    (*env)->DeleteLocalRef(env, win);
    (*env)->DeleteLocalRef(env, cls);
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title; /* NativeActivity 拥有底层 Surface，Canvas 视口自适应缩放 */
    if (width > 0 && height > 0) {
        /* 根据舞台宽高比锁定屏幕方向（横屏/竖屏） */
        ant_set_orientation(width > height);
    }
    /* 沉浸模式：隐藏状态栏与导航栏叠加层 */
    ant_set_immersive();
    return ZAN_ANW_HWND;
}
EXPORT i32 zan_gui_show_window(iptr hwnd_val)         { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_minimize(iptr hwnd_val)            { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_toggle_maximize(iptr hwnd_val)     { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_close_window(iptr hwnd_val) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_aq_lock);
    g_anw.closed = 1;
    ant_invalidate_display_frame_locked();
    aq_push_locked(8, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_aq_lock);
    /* 请求 Activity 退出并销毁 Surface */
    if (g_anw.app && g_anw.app->activity) {
        ANativeActivity_finish(g_anw.app->activity);
    }
    return 0;
}
EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    (void)hwnd_val;
    anw_detach();
    return 0;
}
EXPORT i32 zan_gui_is_maximized(iptr hwnd_val)     { (void)hwnd_val; return 0; }
EXPORT i32 zan_gui_window_visible(iptr hwnd_val)   { (void)hwnd_val; return g_anw.attached; }
EXPORT i32 zan_gui_window_focused(iptr hwnd_val)   { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_titlebar_height(void)           { return 0; }
EXPORT i32 zan_gui_caption_button_width(void)      { return 0; }
EXPORT i32 zan_gui_set_caption_buttons(iptr h, i32 n) { (void)h; (void)n; return 0; }
EXPORT i32 zan_gui_safe_area_top(void)             { return g_safe_area_top; }
EXPORT i32 zan_gui_safe_area_bottom(void)          { return g_safe_area_bottom; }
EXPORT i32 zan_gui_safe_area_left(void)            { return g_safe_area_left; }
EXPORT i32 zan_gui_safe_area_right(void)           { return g_safe_area_right; }
EXPORT i32 zan_gui_keyboard_height(void)           { return g_keyboard_height; }
EXPORT i32 zan_gui_haptic_feedback(i32 type) {
    (void)type;
    return 0;
}

static float g_sensor_accel[3] = { 0.0f, 9.8f, 0.0f };
static float g_sensor_gyro[3]  = { 0.0f, 0.0f, 0.0f };
static float g_sensor_compass  = 0.0f;

EXPORT i32 zan_gui_sensor_get(i32 sensor_type, float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z) return -1;
    if (sensor_type == 1) {
        *out_x = g_sensor_accel[0]; *out_y = g_sensor_accel[1]; *out_z = g_sensor_accel[2];
        return 0;
    } else if (sensor_type == 2) {
        *out_x = g_sensor_gyro[0]; *out_y = g_sensor_gyro[1]; *out_z = g_sensor_gyro[2];
        return 0;
    } else if (sensor_type == 3) {
        *out_x = g_sensor_compass; *out_y = 0.0f; *out_z = 0.0f;
        return 0;
    }
    return -1;
}

EXPORT i32 zan_gui_sensor_set(i32 sensor_type, float x, float y, float z) {
    if (sensor_type == 1) {
        g_sensor_accel[0] = x; g_sensor_accel[1] = y; g_sensor_accel[2] = z;
        return 0;
    } else if (sensor_type == 2) {
        g_sensor_gyro[0] = x; g_sensor_gyro[1] = y; g_sensor_gyro[2] = z;
        return 0;
    } else if (sensor_type == 3) {
        g_sensor_compass = x;
        return 0;
    }
    return -1;
}

EXPORT i32 zan_gui_notification_send(const char *title, const char *body, const char *action, i32 badge, const char *sound) {
    (void)title; (void)body; (void)action; (void)badge; (void)sound;
    return 0;
}
EXPORT i32 zan_gui_set_window_pos(iptr h, i32 x, i32 y) { (void)h; (void)x; (void)y; return 0; }
EXPORT i32 zan_gui_center_window(iptr hwnd_val)    { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_set_topmost(iptr h, i32 on)     { (void)h; (void)on; return 0; }
EXPORT i32 zan_gui_set_title(iptr h, const char *t) { (void)h; (void)t; return 0; }
EXPORT i32 zan_gui_set_cursor(i32 cursor_type)     { (void)cursor_type; return 0; }


EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    /* 待重绘空闲轮询路径：带超时的 epoll/ALooper 等待 */
    ant_pump_looper();
    int woke = ant_take_wake();
    if (aq_pop() || woke) { return 0; }
    return g_anw.closed ? -1 : 1;
}

/* 由请求线程的 Looper 执行；只发布帧信号，不创建或覆盖输入事件。 */
static void ant_display_frame_callback(long frame_time_ns, void *data) {
    (void)frame_time_ns;
    uintptr_t token = (uintptr_t)data;
    pthread_mutex_lock(&g_aq_lock);
    if (token != 0 && token == g_display_frame_pending) {
        g_display_frame_pending = 0;
        if (g_ui_looper && g_anw.app && g_anw.attached && !g_anw.closed &&
            !g_anw.app->destroyRequested) {
            g_display_frame_ready = 1;
            ant_wake_locked();
        }
    }
    pthread_mutex_unlock(&g_aq_lock);
}

static int ant_display_frame_available_locked(ALooper *looper) {
    return looper && looper == g_ui_looper && g_anw.app &&
           g_anw.attached && !g_anw.closed && !g_anw.app->destroyRequested;
}

EXPORT i32 zan_gui_request_display_frame(iptr hwnd_val) {
    if (hwnd_val != ZAN_ANW_HWND) return 0;
    ALooper *looper = ALooper_forThread();
    pthread_mutex_lock(&g_aq_lock);
    if (!ant_display_frame_available_locked(looper)) {
        pthread_mutex_unlock(&g_aq_lock);
        return 0;
    }
    if (g_display_frame_pending || g_display_frame_ready) {
        pthread_mutex_unlock(&g_aq_lock);
        return 1;
    }
    AChoreographer *choreographer = g_display_choreographer;
    /* API24 在当前 Looper 线程取得实例；稳态请求只取一次队列锁。 */
    if (!choreographer) {
        pthread_mutex_unlock(&g_aq_lock);
        choreographer = AChoreographer_getInstance();
        if (!choreographer) return 0;
        pthread_mutex_lock(&g_aq_lock);
        if (!ant_display_frame_available_locked(looper)) {
            pthread_mutex_unlock(&g_aq_lock);
            return 0;
        }
        g_display_choreographer = choreographer;
    }
    if (++g_display_frame_serial == 0) ++g_display_frame_serial;
    uintptr_t token = g_display_frame_serial;
    g_display_frame_pending = token;
    pthread_mutex_unlock(&g_aq_lock);
    AChoreographer_postFrameCallback(choreographer, ant_display_frame_callback,
                                     (void *)token);
    return 1;
}

EXPORT i32 zan_gui_take_display_frame(iptr hwnd_val) {
    if (hwnd_val != ZAN_ANW_HWND) return 0;
    ALooper *looper = ALooper_forThread();
    pthread_mutex_lock(&g_aq_lock);
    int ready = ant_display_frame_available_locked(looper) && g_display_frame_ready;
    if (ready) g_display_frame_ready = 0;
    pthread_mutex_unlock(&g_aq_lock);
    return ready;
}

static int ant_poll_looper(int timeout_ms) {
    struct android_app *app = g_anw.app;
    if (!app || !app->looper) return ALOOPER_POLL_ERROR;
    int events = 0;
    struct android_poll_source *source = NULL;
    int ident = ALooper_pollOnce(timeout_ms, NULL, &events, (void **)&source);
    if (ident >= 0 && source && source->process) source->process(app, source);
    if (app->destroyRequested != 0 && !g_anw.closed) {
        pthread_mutex_lock(&g_aq_lock);
        g_anw.closed = 1;
        ant_invalidate_display_frame_locked();
        aq_push_locked(8, 0, 0, 0, 0, 0);
        pthread_mutex_unlock(&g_aq_lock);
    }
    return ident;
}

static void ant_pump_looper(void) {
    while (ant_poll_looper(0) >= 0) {
        if (g_anw.closed) return;
    }
}

static i32 ant_wait_event(int timeout_ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    ant_pump_looper();
    int woke = ant_take_wake();
    if (aq_pop() || woke) return 0;
    if (g_anw.closed) return -1;
    if (timeout_ms == 0) return 1;
    int ident = ant_poll_looper(timeout_ms);
    /* WAKE 可与输入、callback 或 timeout 合并，序号比返回码更可靠。 */
    woke = ant_take_wake();
    if (aq_pop() || woke) return 0;
    if (g_anw.closed || ident == ALOOPER_POLL_ERROR) return -1;
    return ident == ALOOPER_POLL_TIMEOUT ? 1 : 0;
}

EXPORT i32 zan_gui_wait_event(void) { return ant_wait_event(-1); }

EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    return ant_wait_event(ms < 0 ? 0 : ms);
}

EXPORT i32 zan_gui_wake(void) {
    pthread_mutex_lock(&g_aq_lock);
    ant_wake_locked();
    pthread_mutex_unlock(&g_aq_lock);
    return 0;
}

EXPORT i32 zan_gui_inject_event(
    iptr hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_aq_lock);
    aq_push_locked((int)kind, (int)x, (int)y, (int)button, (int)keycode,
                   (int)mods);
    pthread_mutex_unlock(&g_aq_lock);
    return 0;
}
EXPORT i32 zan_gui_inject_pending(void) {
    return (g_aq_tail - g_aq_head + ZAN_AQ_CAP) % ZAN_AQ_CAP;
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


EXPORT i32 zan_gui_get_dpi_scale(void) { return (i32)(g_dpi * 100 / 96); }

EXPORT i64 zan_gui_get_tick_ms(void) { return ant_tick_ms(); }
EXPORT void zan_gui_sleep_ms(i32 ms) {
    if (ms > 0) {
        struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

EXPORT const char *zan_gui_android_files_dir(void) { return g_files_dir; }

/* JNI 桥接、输入法与系统剪贴板通道由子模块组合引入 */
#include "gui_runtime_android_jni.c"

EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1;
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

#endif /* ZAN_GUI_ANDROID_NATIVE */
