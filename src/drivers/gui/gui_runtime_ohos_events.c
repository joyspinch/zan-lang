/* Shared production event ring and surface lifecycle. Included by the OHOS
 * runtime and native boundary test; neither consumer reimplements these paths. */
static void ohos_ime_invalidate_geometry(void);
static int ohos_ime_pop_operation_locked(int *kind, int *code);
static int ohos_ime_pending_locked(void);
void zan_gui_ohos_ime_shutdown(void);

typedef struct { int e[8]; iptr win; } zan_oev_t;
#define ZAN_OQ_CAP 512
static zan_oev_t g_oq[ZAN_OQ_CAP];
static int g_oq_head, g_oq_tail;
static uint64_t g_oq_written, g_oq_read;
static int g_oq_no_coalesce;
static pthread_mutex_t g_oq_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_pending_event[8];
static iptr g_event_win;
static long long g_ev_seq;

/* Return whether an event was accepted, including a merged move/scroll. */
static int oq_push_locked(int kind, int x, int y, int button, int code, int mods) {
    int last = (g_oq_tail + ZAN_OQ_CAP - 1) % ZAN_OQ_CAP;
    int has_last = g_oq_head != g_oq_tail && !g_oq_no_coalesce;
    if (kind == 1 && has_last && g_oq[last].e[0] == 1) {
        g_oq[last].e[1] = x; g_oq[last].e[2] = y;
        return 1;
    }
    if (kind == 13 && has_last && g_oq[last].e[0] == 13) {
        g_oq[last].e[1] = x; g_oq[last].e[2] = y;
        g_oq[last].e[4] += code;
        return 1;
    }
    int next = (g_oq_tail + 1) % ZAN_OQ_CAP;
    if (next == g_oq_head) return 0;
    zan_oev_t *z = &g_oq[g_oq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = z->e[7] = 0;
    z->win = ZAN_OHOS_HWND;
    g_oq_tail = next;
    ++g_oq_written;
    g_oq_no_coalesce = 0;
    return 1;
}

static void oq_push_flag_locked(int kind, int x, int y, int button, int code, int mods) {
    if (oq_push_locked(kind, x, y, button, code, mods)) {
        int last = (g_oq_tail + ZAN_OQ_CAP - 1) % ZAN_OQ_CAP;
        g_oq[last].e[6] = 1;
    }
}

static int oq_pop(void) {
    pthread_mutex_lock(&g_oq_lock);
    int kind, code;
    if (ohos_ime_pop_operation_locked(&kind, &code)) {
        memset(g_pending_event, 0, sizeof(g_pending_event));
        g_pending_event[0] = kind;
        g_pending_event[4] = code;
        g_event_win = ZAN_OHOS_HWND;
        ++g_ev_seq;
        pthread_mutex_unlock(&g_oq_lock);
        return 1;
    }
    if (g_oq_head == g_oq_tail) { pthread_mutex_unlock(&g_oq_lock); return 0; }
    zan_oev_t *z = &g_oq[g_oq_head];
    memcpy(g_pending_event, z->e, sizeof(g_pending_event));
    g_event_win = z->win;
    g_oq_head = (g_oq_head + 1) % ZAN_OQ_CAP;
    ++g_oq_read;
    ++g_ev_seq;
    pthread_mutex_unlock(&g_oq_lock);
    return 1;
}

EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    return oq_pop() ? 0 : 1;
}

EXPORT i32 zan_gui_inject_event(iptr hwnd_val, i32 kind, i32 x, i32 y,
        i32 button, i32 keycode, i32 mods) {
    (void)hwnd_val;
    pthread_mutex_lock(&g_oq_lock);
    oq_push_locked(kind, x, y, button, keycode, mods);
    pthread_mutex_unlock(&g_oq_lock);
    return 0;
}

EXPORT i32 zan_gui_inject_pending(void) {
    pthread_mutex_lock(&g_oq_lock);
    int pending = (g_oq_tail - g_oq_head + ZAN_OQ_CAP) % ZAN_OQ_CAP;
    if (ohos_ime_pending_locked() && pending == 0) pending = 1;
    pthread_mutex_unlock(&g_oq_lock);
    return pending;
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

/* Native-window presentation setup remains in runtime_ohos.c. This helper is
 * its complete surface state transition, also exercised by the host test. */
static void ohos_window_attach(void *nw, int w, int h) {
    ohos_ime_invalidate_geometry();
    pthread_mutex_lock(&g_oq_lock);
    g_owin.nw = nw;
    g_owin.w = w; g_owin.h = h;
    g_owin.attached = 1;
    g_owin.closed = 0;
    g_window_width = w; g_window_height = h;
    oq_push_locked(7, w, h, 0, 0, 0);
    oq_push_locked(14, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);
}

EXPORT void zan_gui_ohos_detach(void) {
    /* Session cleanup takes its own locks: never call it with g_oq_lock held. */
    zan_gui_ohos_ime_shutdown();
    pthread_mutex_lock(&g_oq_lock);
    g_owin.attached = 0;
    g_owin.nw = NULL;
    pthread_mutex_unlock(&g_oq_lock);
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title; (void)width; (void)height;
    return ZAN_OHOS_HWND;
}

EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    (void)hwnd_val;
    zan_gui_ohos_detach();
    pthread_mutex_lock(&g_oq_lock);
    g_owin.closed = 1;
    g_oq_head = g_oq_tail = 0;
    g_oq_read = g_oq_written;
    g_oq_no_coalesce = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    g_event_win = 0;
    pthread_mutex_unlock(&g_oq_lock);
    return 0;
}
