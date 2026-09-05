/* gui_runtime_sdl.c -- the unified SDL3 window/event/present backend (ZAN_GUI_SDL).
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * Unified SDL3 Window Shell  (ZAN_GUI_SDL)
 * ------------------------------------------------------------------------
 * A single cross-platform windowing/event/present backend built on SDL3 —
 * the same stack the Game.* stdlib uses via zan_sdl3, so the IDE window is an
 * SDL window unified with games. The software rasterizer still paints into a
 * CPU surface; here that surface is uploaded to a per-window streaming texture
 * and presented through the SDL renderer (D3D11/Metal/Vulkan/GL, chosen by
 * SDL). The exported ABI is byte-for-byte the one Gui.Window imports, so no
 * Zan code changes. Borderless chrome + drag/resize is delivered through
 * SDL_SetWindowHitTest, mirroring the old WM_NCHITTEST regions. Event codes,
 * VK keycodes and modifier bits match the Win32 encoding Gui/App.zan expects.
 * ======================================================================== */

#ifdef ZAN_GUI_SDL

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#include <android/log.h>
#endif

/* Borderless-window chrome metrics (device px), reported to Zan so its custom
 * title bar and caption-button hit regions line up with the drag/resize
 * regions the hit-test below carves out. */
static int g_dpi = 96;
static int g_titlebar_h = 32;
static int g_btn_w = 46;
static int g_caption_btn_count = 5;

/* Last reported event, in the shared int[8] protocol:
 * [0]=kind [1]=x [2]=y [3]=button [4]=keycode/char/delta [5]=modifiers. */
static int g_pending_event[8];
static SDL_Window *g_event_win = NULL; /* window that produced g_pending_event */
static SDL_Window *g_main_win = NULL;  /* first window: closing it quits */
static bool g_mouse_captured = false;  /* holding a client-widget drag */
/* Event-arrival stamps for the per-frame profiler line: when the newest event
 * landed and how many events accumulated since the previous present. Splits
 * "input arrived late" (injection/transport) from "frame painted late" (loop
 * pacing) — the difference between a device problem and a loop problem. */
static Uint64 g_last_event_at;         /* perf counter of newest event */
static int    g_ev_count;              /* events since previous present */
static void fprof_ev(const char *what, int ret); /* fwd: defined near fprof */
/* Set when SDL reports the GL device/targets were reset (Android tears the
 * EGL context down on every background cycle) or when a resume-class window
 * event lands while the GL context belongs to another thread. Consumed by
 * zan_gui_present on the app thread: textures are destroyed so the next
 * present recreates them and re-uploads the retained CPU surface in full —
 * otherwise idle presents patch a context-orphaned texture with a few small
 * damage rects and the page stays black behind a live HUD. */
static int g_gl_reset_pending;

/* Manual (non-modal) window resize. Handing a border press to the OS through
 * SDL_HITTEST_RESIZE_* starts Windows' modal move/resize loop, which owns the
 * thread until the button is released: SDL_PollEvent never returns, so the app
 * cannot lay out or repaint and the window shows a frozen frame that only
 * snaps to the new size on release. The border is therefore hit-tested as
 * client area and resized here instead -- ordinary motion events keep flowing,
 * so every drag step produces a normal resize event and a repainted frame. */
#define ZAN_EDGE_LEFT   1
#define ZAN_EDGE_RIGHT  2
#define ZAN_EDGE_TOP    4
#define ZAN_EDGE_BOTTOM 8
static SDL_Window *g_resize_win = NULL; /* window being resized, NULL = none */
static int g_resize_edge = 0;           /* ZAN_EDGE_* mask of the grabbed edge */
static float g_resize_mx0 = 0, g_resize_my0 = 0; /* global pointer at grab */
static int g_resize_x0 = 0, g_resize_y0 = 0;     /* window rect at grab */
static int g_resize_w0 = 0, g_resize_h0 = 0;
static int g_window_width = 0, g_window_height = 0; /* last-resized size (px) */
static int g_sdl_ready = 0;
static int g_quit = 0;

/* Files dropped onto a window: SDL reports one path per SDL_EVENT_DROP_FILE,
 * so they are queued here and reported to Zan as a single kind-9 event
 * carrying the drop position. Zan drains the paths with drop_take(). */
#define ZAN_DROP_CAP 64
static char *g_drop_q[ZAN_DROP_CAP];
static int g_drop_head = 0, g_drop_tail = 0;

static void drop_push(const char *path) {
    if (!path || !*path) return;
    int next = (g_drop_tail + 1) % ZAN_DROP_CAP;
    if (next == g_drop_head) return; /* full: ignore the extra paths */
    size_t n = strlen(path);
    char *copy = (char *)malloc(n + 1);
    if (!copy) return;
    memcpy(copy, path, n + 1);
    g_drop_q[g_drop_tail] = copy;
    g_drop_tail = next;
}
static Uint32 g_wake_event = 0; /* user event type posted by zan_gui_wake */
static int g_ime_x = 0, g_ime_y = 0;

/* Touch gesture state (phones/touchscreens: no wheel exists). The first
 * finger is the only one tracked; a tap under the 8 px slop is delivered
 * as a full synthesized click at finger-up, a drag beyond the slop turns
 * into synthesized wheel events (1:1 finger travel) and presses nothing.
 * SDL's own touch->mouse synthesis is switched off at init. */
static SDL_FingerID g_touch_id = 0;   /* 0 = no finger down */
static float g_touch_x, g_touch_y;    /* last position, window coords */
static float g_touch_ax, g_touch_ay;  /* anchor = down position */
static int g_touch_drag = 0;          /* past the slop: scrolling */
static float g_touch_acc = 0;         /* sub-notch wheel remainder: a notch
                                      * is g_dpi/300 content px, so slow
                                      * drags move well under one notch per
                                      * motion event — without the float
                                      * accumulator the per-event (int)
                                      * truncation ate that travel whole
                                      * and the scroll stalled, then jumped */
#define TOUCH_HIST 8                  /* release-velocity ring buffer */
static float g_hist_y[TOUCH_HIST];
static Uint64 g_hist_t[TOUCH_HIST];
static int g_hist_n, g_hist_i;

/* Release inertia (fling): on finger-up with enough travel speed a timer
 * keeps pushing wheel events with exponentially decaying velocity, like
 * every native mobile scroller. Driven from SDL's timer thread; it only
 * pushes plain SDL_EVENT_MOUSE_WHEEL events (thread-safe SDL_PushEvent),
 * which the main loop translates through the exact same path as real
 * wheel input. */
static SDL_TimerID g_fling_timer = 0; /* 0 = no fling running */
static SDL_WindowID g_fling_win = 0;
static float g_fling_v = 0;           /* px/s, window px, drag-sign convention */
static float g_fling_acc = 0;         /* sub-notch remainder, as above */
static float g_fling_x, g_fling_y;    /* wheel event coords = release point */
static Uint64 g_fling_last = 0;

#define FLING_START_PX_S 250.0f       /* slower releases just stop */
#define FLING_STOP_PX_S 120.0f        /* decay stop band */
#define FLING_TAU_MS 400.0f           /* exponential friction constant */

/* Push `px` content pixels of travel through the wheel path (notch
 * accumulate -> integral ±120-scale degrees), at the given coords. */
static void fling_push_px(float px) {
    g_fling_acc += px * 288.0f / (float)g_dpi;
    int delta = (int)g_fling_acc;
    if (delta == 0) return;
    g_fling_acc -= (float)delta;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_MOUSE_WHEEL;
    ev.wheel.windowID = g_fling_win;
    ev.wheel.mouse_x = g_fling_x;
    ev.wheel.mouse_y = g_fling_y;
    ev.wheel.y = (float)delta / 120.0f;
    SDL_PushEvent(&ev);
}

static Uint32 SDLCALL fling_tick(void *userdata, SDL_TimerID id, Uint32 interval) {
    (void)userdata; (void)id;
    if (g_fling_win == 0) return 0;
    Uint64 now = SDL_GetTicks();
    float dt = (float)(now - g_fling_last) / 1000.0f;
    g_fling_last = now;
    if (dt > 0) {
        g_fling_v *= expf(-dt * 1000.0f / FLING_TAU_MS);
        fling_push_px(g_fling_v * dt);
    }
    if (fabsf(g_fling_v) < FLING_STOP_PX_S) {
        g_fling_win = 0;
        return 0; /* cancel the timer */
    }
    return interval;
}

static void fling_cancel(void) {
    if (g_fling_timer != 0) {
        SDL_RemoveTimer(g_fling_timer);
        g_fling_timer = 0;
    }
    g_fling_win = 0;
    g_fling_v = 0;
    g_fling_acc = 0;
}

/* Per-window renderer + streaming texture registry. The handle handed back to
 * Zan is the SDL_Window* (cast to i64), exactly like the Win32 HWND was. */
typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tw, th;  /* current texture size */
    int           closed;  /* 1 once zan_gui_close_window hid it (pending free) */
    int           caption_btns; /* caption buttons this window draws (per-window
                                 * so a dialog with fewer buttons never corrupts
                                 * the main window's draggable/hit regions) */
} zan_sdl_win_t;
#define ZAN_SDL_MAX_WIN 32
static zan_sdl_win_t g_wins[ZAN_SDL_MAX_WIN];
static int g_win_count = 0;

static zan_sdl_win_t *sdl_find(SDL_Window *w) {
    for (int i = 0; i < g_win_count; i++)
        if (g_wins[i].win == w) return &g_wins[i];
    return NULL;
}

/* Re-present a window's own last frame from its streaming texture. Used for
 * OS-driven invalidations (expose/show/restore) so a window never shows stale
 * or another window's pixels while the app thread is between repaints — e.g.
 * the ghost trails the main window showed while a child dialog opened. */
static void sdl_represent(zan_sdl_win_t *rec) {
    if (!rec || !rec->ren || !rec->tex || rec->closed) return;
    SDL_SetRenderDrawColor(rec->ren, (g_bg_color >> 16) & 0xFF,
                           (g_bg_color >> 8) & 0xFF, g_bg_color & 0xFF, 255);
    SDL_RenderClear(rec->ren);
    SDL_FRect dst = { 0.0f, 0.0f, (float)rec->tw, (float)rec->th };
    SDL_RenderTexture(rec->ren, rec->tex, NULL, &dst);
    SDL_RenderPresent(rec->ren);
}

/* Destroy any window a prior zan_gui_close_window marked closed, freeing its
 * renderer/texture/window and compacting the registry so the 32-slot pool is
 * reclaimed. Win32 destroyed a window synchronously on WM_CLOSE; SDL defers to
 * here (called when the app is no longer inside that window's frame -- i.e. at
 * the next create/present) so we never free a window mid-use. */
static void sdl_reclaim_closed(void) {
    int i = 0;
    while (i < g_win_count) {
        if (g_wins[i].closed) {
            SDL_Window *w = g_wins[i].win;
            if (g_wins[i].tex) SDL_DestroyTexture(g_wins[i].tex);
            if (g_wins[i].ren) SDL_DestroyRenderer(g_wins[i].ren);
            if (w) SDL_DestroyWindow(w);
            if (g_event_win == w) g_event_win = NULL;
            for (int j = i + 1; j < g_win_count; j++) g_wins[j - 1] = g_wins[j];
            g_win_count--;
        } else {
            i++;
        }
    }
}

/* --- internal event queue: one SDL event may yield 0..N zan events (e.g. an
 * IME text commit expands to one kind-6 per codepoint). ------------------- */
typedef struct { int e[8]; SDL_Window *win; } zan_zev_t;
#define ZAN_ZQ_CAP 512
static zan_zev_t g_zq[ZAN_ZQ_CAP];
static int g_zq_head = 0, g_zq_tail = 0;

static int zq_empty(void) { return g_zq_head == g_zq_tail; }

static void zq_push(int kind, int x, int y, int button, int code, int mods,
                    SDL_Window *win) {
    /* Coalesce mouse-move floods: unlike Win32 (which collapses WM_MOUSEMOVE),
     * SDL emits one motion event per sample, so a fast drag can enqueue
     * hundreds. The app consumes one event per ~16ms frame, so an un-coalesced
     * backlog makes the UI (and hover highlights) lag seconds behind the
     * cursor. If the last unconsumed event is also a plain move on the same
     * window, overwrite it with the newer position instead of enqueuing. */
    if (kind == 1 && !zq_empty()) {
        int last = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
        zan_zev_t *p = &g_zq[last];
        if (p->e[0] == 1 && p->win == win) {
            p->e[1] = x; p->e[2] = y; p->e[5] = mods;
            return;
        }
    }
    /* Coalesce wheel floods the same way, by SUMMING deltas: touch drags
     * translate finger motion into one kind-13 per sample and the app eats
     * one event per rendered frame, so an un-coalesced wheel backlog lags
     * the page visibly behind the finger (a fast flick kept scrolling long
     * after lift while the early travel crawled). Deltas are additive and
     * the freshest x/y wins, so a merged event scrolls exactly the same
     * total in one frame — and capture-rect arbitration sees the position
     * the pointer actually reached. */
    if (kind == 13 && !zq_empty()) {
        int last = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
        zan_zev_t *p = &g_zq[last];
        if (p->e[0] == 13 && p->win == win) {
            p->e[1] = x; p->e[2] = y; p->e[4] += code; p->e[5] = mods;
            return;
        }
    }
    int next = (g_zq_tail + 1) % ZAN_ZQ_CAP;
    if (next == g_zq_head) return; /* full: drop oldest-safe (never overwrite) */
    zan_zev_t *z = &g_zq[g_zq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = 0; z->e[7] = 0;
    z->win = win;
    g_zq_tail = next;
}

/* Pop the front zan event into the reported slots. Caller checks !zq_empty. */
static long long g_ev_seq_sdl = 0;
static void zq_pop(void) {
    zan_zev_t *z = &g_zq[g_zq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_zq_head = (g_zq_head + 1) % ZAN_ZQ_CAP;
    g_ev_seq_sdl++;
}

/* Map an SDL keycode to the Win32 virtual-key code Gui/Keys expects. Letters
 * and digits share their ASCII value with VK codes; the rest are switched. */
static int sdl_key_to_vk(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return 65 + (int)(k - 'a'); /* VK 'A'..'Z' */
    if (k >= '0' && k <= '9') return (int)k;              /* VK '0'..'9' */
    switch (k) {
        case SDLK_ESCAPE:    return 27;
        case SDLK_RETURN:    return 13;
        case SDLK_KP_ENTER:  return 13;
        case SDLK_TAB:       return 9;
        case SDLK_BACKSPACE: return 8;
        case SDLK_DELETE:    return 46;
        case SDLK_INSERT:    return 45;
        case SDLK_LEFT:      return 37;
        case SDLK_UP:        return 38;
        case SDLK_RIGHT:     return 39;
        case SDLK_DOWN:      return 40;
        case SDLK_HOME:      return 36;
        case SDLK_END:       return 35;
        case SDLK_PAGEUP:    return 33;
        case SDLK_PAGEDOWN:  return 34;
        case SDLK_SPACE:     return 32;
        case SDLK_F1:  return 112; case SDLK_F2:  return 113;
        case SDLK_F3:  return 114; case SDLK_F4:  return 115;
        case SDLK_F5:  return 116; case SDLK_F6:  return 117;
        case SDLK_F7:  return 118; case SDLK_F8:  return 119;
        case SDLK_F9:  return 120; case SDLK_F10: return 121;
        case SDLK_F11: return 122; case SDLK_F12: return 123;
        default:             return (k < 128) ? (int)k : 0;
    }
}

static int sdl_mods_to_bits(SDL_Keymod m) {
    int r = 0;
    if (m & SDL_KMOD_CTRL)  r |= 1;
    if (m & SDL_KMOD_SHIFT) r |= 2;
    if (m & SDL_KMOD_ALT)   r |= 4;
    return r;
}

static int cur_mod_bits(void) { return sdl_mods_to_bits(SDL_GetModState()); }

/* Defined below; used by the event translator to decide whether a press should
 * capture the mouse (client widget) or hand off to the OS move/resize loop. */
static SDL_HitTestResult SDLCALL zan_sdl_hittest(SDL_Window *win,
                                                 const SDL_Point *area,
                                                 void *data);
static int zan_sdl_resize_edge(SDL_Window *win, int x, int y);
static void zan_sdl_resize_step(void);

/* Decode one UTF-8 codepoint from s (advancing *i); returns -1 at end. */
static int sdl_utf8_next(const char *s, int *i) {
    unsigned char c = (unsigned char)s[*i];
    if (c == 0) return -1;
    int cp, n;
    if (c < 0x80)      { cp = c;        n = 1; }
    else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
    else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
    else               { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[*i + k];
        if ((cc & 0xC0) != 0x80) { n = k; break; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *i += n;
    return cp;
}

/* Translate one SDL event into 0..N queued zan events. */
static void sdl_translate(const SDL_Event *e) {
    g_last_event_at = SDL_GetPerformanceCounter();
    g_ev_count++;
    switch (e->type) {
        case SDL_EVENT_QUIT:
            zq_push(8, 0, 0, 0, 0, 0, g_main_win);
            g_quit = 1;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            /* OS-initiated close (e.g. Alt+F4 on a borderless window). Same
             * handling as an app-driven Close(): quit on the main window, hide
             * + reclaim a secondary one so it disappears instead of lingering. */
            SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
            zq_push(8, 0, 0, 0, 0, 0, w);
            if (w == g_main_win) {
                g_quit = 1;
            } else {
                zan_sdl_win_t *rec = sdl_find(w);
                if (rec && !rec->closed) { SDL_HideWindow(w); rec->closed = 1; }
            }
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
            int pw = 0, ph = 0;
            if (w) SDL_GetWindowSizeInPixels(w, &pw, &ph);
            if (pw > 0 && ph > 0) { g_window_width = pw; g_window_height = ph; }
            zq_push(7, pw, ph, 0, 0, 0, w);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            SDL_Window *w = SDL_GetWindowFromID(e->motion.windowID);
            /* Safety net: if capture is somehow still held with no button down
             * (e.g. an OS move/resize loop swallowed the button-up), drop it so
             * the pointer can't get stuck captured. */
            if (g_mouse_captured && (e->motion.state & SDL_BUTTON_LMASK) == 0) {
                SDL_CaptureMouse(false);
                g_mouse_captured = false;
            }
            /* A manual border drag owns the pointer: resize instead of
             * reporting motion, so no control sees a phantom drag. */
            if (g_resize_win) {
                if ((e->motion.state & SDL_BUTTON_LMASK) == 0) {
                    g_resize_win = NULL;
                    g_resize_edge = 0;
                    SDL_CaptureMouse(false);
                } else {
                    zan_sdl_resize_step();
                }
                break;
            }
            zq_push(1, (int)e->motion.x, (int)e->motion.y, 0, 0,
                    cur_mod_bits(), w);
            /* A real pointer move is never a touch twin: whether this push
             * landed fresh or merged into a tagged twin, the resulting entry
             * describes the physical pointer and must read untagged. */
            if (!zq_empty()) {
                int ul = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
                g_zq[ul].e[6] = 0;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            SDL_Window *w = SDL_GetWindowFromID(e->button.windowID);
            int btn = (e->button.button == SDL_BUTTON_RIGHT) ? 1 : 0;
            int down = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            int kind = down ? 2 : 3;
            /* Mirror Win32 SetCapture/ReleaseCapture so a held widget drag
             * (slider/scrollbar/selection) keeps getting motion + the button-up
             * even after the pointer leaves the window. Only capture when the
             * press lands on a client widget (SDL_HITTEST_NORMAL): a press on
             * the draggable caption or a resize border starts an OS modal
             * move/resize loop that swallows the button-up, which would leave
             * the mouse captured forever and break all later input routing. */
            if (e->button.button == SDL_BUTTON_LEFT) {
                if (down) {
                    /* Border press: start a manual resize and swallow the
                     * click so it never reaches a control underneath. */
                    int edge = w ? zan_sdl_resize_edge(w, (int)e->button.x,
                                                       (int)e->button.y) : 0;
                    if (edge) {
                        g_resize_win = w;
                        g_resize_edge = edge;
                        SDL_GetGlobalMouseState(&g_resize_mx0, &g_resize_my0);
                        SDL_GetWindowPosition(w, &g_resize_x0, &g_resize_y0);
                        SDL_GetWindowSize(w, &g_resize_w0, &g_resize_h0);
                        SDL_CaptureMouse(true);
                        break;
                    }
                    SDL_Point p = { (int)e->button.x, (int)e->button.y };
                    if (w && zan_sdl_hittest(w, &p, NULL) == SDL_HITTEST_NORMAL) {
                        SDL_CaptureMouse(true);
                        g_mouse_captured = true;
                    }
                } else {
                    if (g_resize_win) {
                        g_resize_win = NULL;
                        g_resize_edge = 0;
                        SDL_CaptureMouse(false);
                        break;
                    }
                    if (g_mouse_captured) {
                        SDL_CaptureMouse(false);
                        g_mouse_captured = false;
                    }
                }
            }
            zq_push(kind, (int)e->button.x, (int)e->button.y, btn, 0,
                    cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_RESTORED: {
            /* The OS invalidated this window (uncovered, shown, restored, or a
             * neighbouring window's open animation). Repaint it from its own
             * last frame right away instead of leaving stale pixels. */
            SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
#if defined(__ANDROID__)
            /* NEVER call GL from here: on Android events are translated on
             * the Java main thread (zan_event_watch) while the EGL context
             * and every texture live on the app thread — an sdl_represent
             * from this thread is a silent no-op at best. The background
             * cycle dropped the context anyway (texture contents gone).
             * Flag the reset; the next present on the app thread recreates
             * the texture and re-uploads the retained CPU surface in full. */
            (void)w;
            g_gl_reset_pending = 1;
#else
            sdl_represent(sdl_find(w));
#endif
            break;
        }
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_TARGETS_RESET:
            g_gl_reset_pending = 1;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
            /* Clear widget hover when the pointer leaves the window. Skipped
             * while a button is held: that drag still owns the pointer via the
             * capture above and must keep tracking. */
            if (SDL_GetMouseState(NULL, NULL) == 0) {
                SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
                zq_push(1, -1, -1, 0, 0, cur_mod_bits(), w);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            SDL_Window *w = SDL_GetWindowFromID(e->wheel.windowID);
            /* Report a Win32-style ±120 delta so Gui/App's /120 math holds. */
            int delta = (int)(e->wheel.y * 120.0f);
            zq_push(13, (int)e->wheel.mouse_x, (int)e->wheel.mouse_y, 0,
                    delta, cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_FINGER_DOWN: {
            if (g_touch_id != 0) break; /* only the first finger scrolls */
            /* Touch events may arrive without a usable windowID (some
             * backends route them to 0); Android is single-window anyway. */
            SDL_Window *w = SDL_GetWindowFromEvent(e);
            if (!w) w = g_main_win;
            if (!w) break;
            int wi = 1, hi = 1;
            SDL_GetWindowSize(w, &wi, &hi);
            g_touch_id = e->tfinger.fingerID;
            g_touch_x = g_touch_ax = e->tfinger.x * (float)wi;
            g_touch_y = g_touch_ay = e->tfinger.y * (float)hi;
            g_touch_drag = 0;
            g_touch_acc = 0;
            fling_cancel(); /* a new touch always kills a coasting fling */
            g_hist_n = 0; g_hist_i = 0;
            g_hist_t[0] = SDL_GetTicks();
            g_hist_y[0] = g_touch_y;
            g_hist_n = 1; g_hist_i = 1 % TOUCH_HIST;
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            if (e->tfinger.fingerID != g_touch_id || g_touch_id == 0) break;
            SDL_Window *w = SDL_GetWindowFromEvent(e);
            if (!w) w = g_main_win;
            if (!w) break;
            int wi = 1, hi = 1;
            SDL_GetWindowSize(w, &wi, &hi);
            float x = e->tfinger.x * (float)wi, y = e->tfinger.y * (float)hi;
            if (!g_touch_drag) {
                float dx = x - g_touch_ax, dy = y - g_touch_ay;
                if (dx * dx + dy * dy < 64.0f) break; /* slop: still a tap */
                g_touch_drag = 1;
                /* A drag may start on a draggable widget (window title bar,
                 * resize handle, slider) just as well as on scrollable
                 * content. Synthesize the mouse press at the touch anchor so
                 * Gui-level hit testing sees a press and the widget can own
                 * the gesture; every motion below also reports mouse movement.
                 * Scrollable areas keep their wheel stream — the two flows
                 * coexist, and a wheel only scrolls what the finger is over
                 * (popup blockers already sink it below floating layers). */
                zq_push(2, (int)g_touch_ax, (int)g_touch_ay, 0, 0,
                        cur_mod_bits(), w);
            }
            /* Finger travel -> wheel deltas in the ±120 scale Gui/App's
             * /120 math expects: content px per wheel degree is
             * scrollSpeed/120 = (40*dpiScale/100)/120 = dpiScale/300, and
             * dpiScale = g_dpi*100/96, so degrees = dy*288/g_dpi moves
             * content by exactly dy window px. Sub-degree remainders
             * accumulate in g_touch_acc — a per-event (int) cast there
             * made slow drags stall, then jump. The sign mirrors natural
             * touch scrolling: content follows the finger, so dragging up
             * (y shrinks) must scroll DOWN, i.e. negative wheel delta. */
            g_touch_acc += (y - g_touch_y) * 288.0f / (float)g_dpi;
            int delta = (int)g_touch_acc;
            if (delta != 0) {
                g_touch_acc -= (float)delta;
                zq_push(13, (int)x, (int)y, 0, delta, cur_mod_bits(), w);
            }
            /* release-velocity history (recent window only, so a reversal
             * right before finger-up flings in the new direction) */
            g_hist_t[g_hist_i] = SDL_GetTicks();
            g_hist_y[g_hist_i] = y;
            g_hist_i = (g_hist_i + 1) % TOUCH_HIST;
            if (g_hist_n < TOUCH_HIST) g_hist_n++;
            g_touch_x = x;
            g_touch_y = y;
            /* Sub-notch pointer twin: Gui reads pointer position from wheel
             * events too (App updates mouseX/Y from kind 13), so a sample
             * that carried a wheel delta needs NO extra motion event — the
             * wheel IS the motion. Pushing one anyway made the queue
             * alternate wheel,move,wheel,move, which defeats the wheel
             * flood-merge above: a 60Hz finger on a slow-to-render page
             * (glass theme, ~6fps) backed the queue up tens of seconds
             * deep and the page kept scrolling long after lift-off. Push a
             * motion twin only for sub-notch travel (delta == 0) so held
             * widgets still follow at fine granularity; consecutive plain
             * moves merge, so slow fingers cost one queue slot total. */
            if (delta == 0) {
                int t0 = g_zq_tail;
                zq_push(1, (int)x, (int)y, 0, 0, cur_mod_bits(), w);
                if (g_zq_tail != t0) {
                    int ul = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
                    g_zq[ul].e[6] = 1;
                }
            }
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (e->tfinger.fingerID != g_touch_id || g_touch_id == 0) break;
            g_touch_id = 0;
            if (g_touch_drag) {
                /* The drag drove widgets as well as scroll: report the
                 * mouse release at the finger position, flagged (e[6]) so
                 * the app swallows it as a click — the finger lifted
                 * wherever the drag ended, and treating that as a click
                 * would press whatever sits under the release point. */
                SDL_Window *uw = SDL_GetWindowFromEvent(e);
                if (!uw) uw = g_main_win;
                if (uw) {
                    zq_push(3, (int)g_touch_x, (int)g_touch_y, 0, 0,
                            cur_mod_bits(), uw);
                    int ul = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
                    g_zq[ul].e[6] = 1;
                }
                /* Release inertia: velocity from the recent travel window
                 * (oldest sample still inside ~120 ms). CANCELED just
                 * stops. Coordinates stay at the release point so the
                 * wheel events land on the widget that was being dragged. */
                if (e->type == SDL_EVENT_FINGER_UP) {
                    int last = (g_hist_i + TOUCH_HIST - 1) % TOUCH_HIST;
                    int old = last; /* oldest sample inside the window */
                    for (int k = 0; k < g_hist_n; k++) {
                        int idx = (last - k + TOUCH_HIST) % TOUCH_HIST;
                        if (g_hist_t[idx] + 120 < g_hist_t[last]) break;
                        old = idx;
                    }
                    Uint64 span = g_hist_t[last] - g_hist_t[old];
                    if (span > 0 && g_hist_n >= 2) {
                        float v = (g_hist_y[last] - g_hist_y[old]) * 1000.0f / (float)span;
                        if (fabsf(v) > FLING_START_PX_S) {
                            SDL_Window *w = SDL_GetWindowFromEvent(e);
                            if (!w) w = g_main_win;
                            if (w) {
                                g_fling_v = v;
                                g_fling_acc = 0;
                                g_fling_x = g_touch_x;
                                g_fling_y = g_touch_y;
                                g_fling_win = SDL_GetWindowID(w);
                                g_fling_last = SDL_GetTicks();
                                g_fling_timer = SDL_AddTimer(16, fling_tick, NULL);
                            }
                        }
                    }
                }
                g_touch_drag = 0;
                break;
            }
            if (e->type == SDL_EVENT_FINGER_CANCELED) break;
            /* Tap: a full click at the anchor (move so hover/state is
             * right, then press + release). */
            SDL_Window *w = SDL_GetWindowFromEvent(e);
            if (!w) w = g_main_win;
            if (!w) break;
            int x = (int)g_touch_ax, y = (int)g_touch_ay;
            zq_push(1, x, y, 0, 0, cur_mod_bits(), w);
            zq_push(2, x, y, 0, 0, cur_mod_bits(), w);
            zq_push(3, x, y, 0, 0, cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            SDL_Window *w = SDL_GetWindowFromID(e->key.windowID);
            /* Android's back gesture/button arrives as the AC_BACK
             * scancode. Surface it as a kind-8 close event and let the
             * app decide: a modal may consume it to just close (drawer),
             * so g_quit must stay clear — the app ends itself by breaking
             * its loop on an unconsumed kind 8, which returns from main
             * and lets SDLActivity finish the task. */
            if (e->key.scancode == SDL_SCANCODE_AC_BACK) {
                if (!w) w = g_main_win;
                zq_push(8, 0, 0, 0, 0, 0, w);
                break;
            }
            int vk = sdl_key_to_vk(e->key.key);
            int kmods = sdl_mods_to_bits(e->key.mod);
            zq_push(4, 0, 0, 0, vk, kmods, w);
            /* SDL's TEXT_INPUT event never delivers control characters,
             * but the widget layer was built on the Win32 WM_CHAR contract
             * (Backspace=8, Enter=13, Ctrl+letter=1..26, Ctrl+/=31).
             * Synthesize the matching kind-6 char event so text widgets
             * (Input / TextArea / CodeEditor) keep editing under SDL3. */
            {
                int ctrl = (e->key.mod & SDL_KMOD_CTRL) != 0;
                int ch = 0;
                if (e->key.key == SDLK_BACKSPACE) ch = 8;
                else if (e->key.key == SDLK_RETURN ||
                         e->key.key == SDLK_KP_ENTER) ch = 13;
                else if (ctrl && e->key.key >= 'a' && e->key.key <= 'z')
                    ch = (int)(e->key.key - 'a' + 1);
                else if (ctrl && e->key.key == '/') ch = 31;
                if (ch != 0) zq_push(6, 0, 0, 0, ch, kmods, w);
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            SDL_Window *w = SDL_GetWindowFromID(e->key.windowID);
            zq_push(5, 0, 0, 0, sdl_key_to_vk(e->key.key),
                    sdl_mods_to_bits(e->key.mod), w);
            break;
        }
        case SDL_EVENT_DROP_FILE: {
            /* One event per file; the app reads the paths off the queue when
             * it sees the kind-9 event (drop position in x/y). */
            SDL_Window *w = SDL_GetWindowFromID(e->drop.windowID);
            drop_push(e->drop.data);
            zq_push(9, (int)e->drop.x, (int)e->drop.y, 0, 0, cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            SDL_Window *w = SDL_GetWindowFromID(e->text.windowID);
            const char *txt = e->text.text;
            int i = 0, cp;
            /* One kind-6 (WM_CHAR-equivalent) per codepoint of the commit. */
            while ((cp = sdl_utf8_next(txt, &i)) > 0)
                zq_push(6, 0, 0, 0, cp, 0, w);
            break;
        }
        default:
            /* g_wake_event and everything else: no zan event. */
            break;
    }
}

/* Which resize border (if any) the point lies in, as a ZAN_EDGE_* mask. Zero
 * for the client area, a maximized window, the caption buttons, or a spot a
 * control has claimed through the hit guard (a scrollbar flush with the
 * edge). This is the geometry the old WM_NCHITTEST regions used; the press is
 * now handled in-process (see g_resize_win) rather than by the OS. */
static int zan_sdl_resize_edge(SDL_Window *win, int x, int y) {
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    int b = 8 * g_dpi / 96;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) return 0;
    /* Fixed-size windows (no SDL_WINDOW_RESIZABLE): no border zones at all —
     * the game creates its canvas once from the initial output size, so a
     * border drag would only resize the window around a picture that never
     * follows. Edge hittest stays off, matching the missing OS sizing grips. */
    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_RESIZABLE)) return 0;
    zan_sdl_win_t *rec = sdl_find(win);
    int nbtn = rec ? rec->caption_btns : g_caption_btn_count;
    if (y < g_titlebar_h && x >= W - nbtn * g_btn_w) return 0;
    int left = x < b, right = x >= W - b, top = y < b, bottom = y >= H - b;
    int corner = (top && left) || (top && right) || (bottom && left)
        || (bottom && right);
    /* Corners always win; a straight edge yields to a control that reserved
     * those last few pixels. */
    if (!corner && zan_gui_in_hit_guard((iptr)(intptr_t)win, x, y)) return 0;
    int mask = 0;
    if (left)   mask |= ZAN_EDGE_LEFT;
    if (right)  mask |= ZAN_EDGE_RIGHT;
    if (top)    mask |= ZAN_EDGE_TOP;
    if (bottom) mask |= ZAN_EDGE_BOTTOM;
    return mask;
}

/* The resize cursor for an edge mask, or NULL for the client area. */
static SDL_SystemCursor zan_sdl_edge_cursor(int edge) {
    if ((edge & ZAN_EDGE_TOP) && (edge & ZAN_EDGE_LEFT))
        return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    if ((edge & ZAN_EDGE_BOTTOM) && (edge & ZAN_EDGE_RIGHT))
        return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    if ((edge & ZAN_EDGE_TOP) && (edge & ZAN_EDGE_RIGHT))
        return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    if ((edge & ZAN_EDGE_BOTTOM) && (edge & ZAN_EDGE_LEFT))
        return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    if (edge & (ZAN_EDGE_LEFT | ZAN_EDGE_RIGHT))
        return SDL_SYSTEM_CURSOR_EW_RESIZE;
    return SDL_SYSTEM_CURSOR_NS_RESIZE;
}

/* Apply one step of a manual resize from the current global pointer. */
static void zan_sdl_resize_step(void) {
    if (!g_resize_win) return;
    float mx = 0, my = 0;
    SDL_GetGlobalMouseState(&mx, &my);
    int dx = (int)(mx - g_resize_mx0), dy = (int)(my - g_resize_my0);
    int x = g_resize_x0, y = g_resize_y0;
    int w = g_resize_w0, h = g_resize_h0;
    int minW = 320 * g_dpi / 96, minH = 240 * g_dpi / 96;
    if (g_resize_edge & ZAN_EDGE_LEFT) {
        if (w - dx < minW) dx = w - minW;
        x += dx; w -= dx;
    } else if (g_resize_edge & ZAN_EDGE_RIGHT) {
        w += dx;
        if (w < minW) w = minW;
    }
    if (g_resize_edge & ZAN_EDGE_TOP) {
        if (h - dy < minH) dy = h - minH;
        y += dy; h -= dy;
    } else if (g_resize_edge & ZAN_EDGE_BOTTOM) {
        h += dy;
        if (h < minH) h = minH;
    }
    int cw = 0, ch = 0;
    SDL_GetWindowSize(g_resize_win, &cw, &ch);
    if (g_resize_edge & (ZAN_EDGE_LEFT | ZAN_EDGE_TOP))
        SDL_SetWindowPosition(g_resize_win, x, y);
    if (w != cw || h != ch) SDL_SetWindowSize(g_resize_win, w, h);
}

/* Borderless drag regions — the SDL analogue of WM_NCHITTEST. The resize
 * border is deliberately reported as client area (see g_resize_win): only the
 * caption strip still hands off to the OS, where a modal loop costs nothing
 * because moving a window needs no repaint. */
static SDL_HitTestResult SDLCALL zan_sdl_hittest(SDL_Window *win,
                                                 const SDL_Point *area,
                                                 void *data) {
    (void)data;
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    int x = area->x, y = area->y;
    zan_sdl_win_t *rec = sdl_find(win);
    int nbtn = rec ? rec->caption_btns : g_caption_btn_count;
    int inButtons = (y < g_titlebar_h && x >= W - nbtn * g_btn_w);
    if (inButtons) return SDL_HITTEST_NORMAL;
    if (zan_sdl_resize_edge(win, x, y)) return SDL_HITTEST_NORMAL;
    if (y < g_titlebar_h) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

#if defined(__ANDROID__)
static int zq_count(void) {
    return (g_zq_tail + ZAN_ZQ_CAP - g_zq_head) % ZAN_ZQ_CAP;
}

static bool SDLCALL zan_event_watch(void *userdata, SDL_Event *e);
static int zan_watch_wait(int ms);
static int zan_watch_installed = 0;
#endif

static void zan_sdl_ensure_init(void) {
    if (g_sdl_ready) return;
    SDL_SetMainReady();
    /* Deliver the click that raises/focuses a background window instead of
     * swallowing it, so a button clicked while the window is inactive fires on
     * the first click rather than needing a second (focus-then-click). */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    /* Touch is handled by the finger-event branch below (tap = click,
     * drag = synthesized wheel); SDL's own touch->mouse synthesis would
     * phantom-press every widget the finger slides across. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) return;
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    g_dpi = (int)(scale * 96.0f + 0.5f);
    g_titlebar_h = 32 * g_dpi / 96;
    g_btn_w = 46 * g_dpi / 96;
    g_wake_event = SDL_RegisterEvents(1);
#if defined(__ANDROID__)
    /* See zan_event_watch / zan_watch_wait (below): Android's SDL wait
     * functions spin instead of blocking, so waits go through the
     * watch-fed zan queue and sleep for real. */
    SDL_AddEventWatch(zan_event_watch, NULL);
    zan_watch_installed = 1;
#endif
    g_sdl_ready = 1;
}

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    zan_sdl_ensure_init();
    sdl_reclaim_closed(); /* free windows a previous Close() hid, reclaim slots */
    if (!g_sdl_ready || g_win_count >= ZAN_SDL_MAX_WIN) return 0;

    int w = (int)width * g_dpi / 96;
    int h = (int)height * g_dpi / 96;
    SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_HIDDEN;
    SDL_Window *win = SDL_CreateWindow(title ? title : "", w, h, flags);
    if (!win) return 0;
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_DestroyWindow(win); return 0; }
    /* Desktop: app paces its own frames (needsRedraw + 16ms sleep); leave
     * VSync off so a present never blocks the UI thread up to a refresh
     * interval. Android: vsync is what kills touch latency — with interval 0
     * the GL queue renders ahead and the screen shows a frame the app painted
     * 1-2 frames ago, while unthrottled swaps fight the app's pacing into
     * jitter. Interval 1 pins present to the display deadline; the app's
     * 16ms sleep goes inert on its own (its `since` is measured from present
     * end, which the vblank block already stretched to a full frame). */
#if defined(__ANDROID__)
    SDL_SetRenderVSync(ren, 1);
#else
    SDL_SetRenderVSync(ren, 0);
#endif
    SDL_SetWindowHitTest(win, zan_sdl_hittest, NULL);
#ifdef _WIN32
    /* SDL borderless windows strip the native frame and with it the DWM drop
     * shadow. Re-extend a 1px frame into the client area so the compositor
     * paints the standard window shadow -- matches the old Win32 shell and the
     * IDE. The frame stays invisible because the window is borderless. */
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(win),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd) {
            MARGINS m = { 1, 1, 1, 1 };
            DwmExtendFrameIntoClientArea(hwnd, &m);
        }
        /* SDL only ever shows the icon handed to SDL_SetWindowIcon, so a
         * program carrying an icon resource (zanc embeds one into every
         * Windows exe) would still run with the blank default in the taskbar
         * and Alt-Tab. Load resource id 1 -- what zanc's RT_GROUP_ICON uses --
         * and hand it to the native window. */
        if (hwnd) {
            HINSTANCE inst = GetModuleHandleW(NULL);
            HICON ico_big = (HICON)LoadImageW(
                inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                LR_DEFAULTCOLOR);
            HICON ico_small = (HICON)LoadImageW(
                inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR);
            if (ico_big)
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)ico_big);
            if (ico_small)
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ico_small);
        }
    }
#endif

#ifdef _WIN32
    /* A borderless SDL window loses the OS drop shadow. SDL keeps WS_THICKFRAME
     * on a resizable window and hides the frame via WM_NCCALCSIZE, so extending
     * the DWM frame by a 1px sheet on the native HWND re-enables the system
     * shadow (and Win11 rounded corners) -- the same trick the old Win32 shell
     * used. The 1px is fully covered by the presented texture. */
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(win),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd) {
            MARGINS m = {0, 0, 0, 1};
            DwmExtendFrameIntoClientArea(hwnd, &m);
        }
    }
#endif

    zan_sdl_win_t *rec = &g_wins[g_win_count++];
    rec->win = win; rec->ren = ren; rec->tex = NULL; rec->tw = rec->th = 0;
    rec->closed = 0;
    rec->caption_btns = g_caption_btn_count;

    if (!g_main_win) {
        g_main_win = win;
        g_window_width = w; g_window_height = h;
    } else {
        /* Center a secondary window (dialog) over the main window, clamped to
         * the display's usable area — mirrors the Win32 dialog placement. */
        int mx = 0, my = 0, mw = 0, mh = 0;
        SDL_GetWindowPosition(g_main_win, &mx, &my);
        SDL_GetWindowSize(g_main_win, &mw, &mh);
        int x = mx + (mw - w) / 2, y = my + (mh - h) / 2;
        SDL_Rect ub;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &ub)) {
            if (x < ub.x) x = ub.x;
            if (y < ub.y) y = ub.y;
            if (x + w > ub.x + ub.w) x = ub.x + ub.w - w;
            if (y + h > ub.y + ub.h) y = ub.y + ub.h - h;
        }
        SDL_SetWindowPosition(win, x, y);
    }
    return (iptr)win;
}

EXPORT i32 zan_gui_show_window(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
#ifdef _WIN32
    /* SDL_RaiseWindow does not reliably steal the OS foreground for a
     * borderless secondary window, so mouse/keyboard input keeps routing
     * to whatever window was active (usually the parent below). Force the
     * activation the way a normal dialog would, so the child owns input. */
    {
        HWND chwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(win),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (chwnd) {
            /* Windows blocks SetForegroundWindow from a thread that does
             * not own the current foreground; briefly attach to the active
             * window's input thread so the activation is allowed. */
            HWND fg = GetForegroundWindow();
            DWORD fgTid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
            DWORD myTid = GetCurrentThreadId();
            if (fgTid && fgTid != myTid) {
                AttachThreadInput(myTid, fgTid, TRUE);
            }
            SetForegroundWindow(chwnd);
            SetActiveWindow(chwnd);
            SetFocus(chwnd);
            BringWindowToTop(chwnd);
            if (fgTid && fgTid != myTid) {
                AttachThreadInput(myTid, fgTid, FALSE);
            }
        }
    }
#endif
    /* Route typed text (incl. IME commits) to this window. Android keeps
     * the session closed until a text-editable takes focus
     * (zan_gui_set_ime_open): a session latched open here summons the
     * soft keyboard over the whole app with nothing to type into. */
#ifndef __ANDROID__
    SDL_StartTextInput(win);
#endif
    return 0;
}

EXPORT i32 zan_gui_minimize(iptr hwnd_val) {
    SDL_MinimizeWindow((SDL_Window *)(intptr_t)hwnd_val);
    return 0;
}

EXPORT i32 zan_gui_toggle_maximize(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(win);
    else SDL_MaximizeWindow(win);
    return 0;
}

EXPORT i32 zan_gui_close_window(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    /* Deliver the close (kind 8) so the owning App drops the window, mirroring
     * the async Win32 WM_CLOSE. Closing the main window quits the app; a
     * secondary window (dialog) must actually disappear -- SDL won't destroy it
     * for us the way Win32/X11 did -- so hide it now and mark it for reclaim on
     * the next create (the app is done with it once it has processed kind 8). */
    zq_push(8, 0, 0, 0, 0, 0, win);
    if (win == g_main_win) {
        g_quit = 1;
    } else {
        zan_sdl_win_t *rec = sdl_find(win);
        if (rec && !rec->closed) {
            SDL_HideWindow(win);
            rec->closed = 1;
        }
    }
    return 0;
}

/* Actually tear down a (non-main) window: SDL_close_window only enqueues a
 * kind-8 event so the app can react; the owner calls this once it has fully
 * unwound its state, so a dialog does not linger on screen as an orphan. */
EXPORT i32 zan_gui_destroy_window(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win || win == g_main_win) return 1;
    /* Drop any still-queued events aimed at this window so a later pop
     * never dereferences a destroyed SDL_Window. */
    for (int i = g_zq_head; i != g_zq_tail; i = (i + 1) % ZAN_ZQ_CAP) {
        if (g_zq[i].win == win) { g_zq[i].e[0] = 0; g_zq[i].win = NULL; }
    }
    if (g_event_win == win) g_event_win = NULL;
    zan_sdl_win_t *rec = sdl_find(win);
    if (rec) {
        if (rec->tex) SDL_DestroyTexture(rec->tex);
        if (rec->ren) SDL_DestroyRenderer(rec->ren);
        SDL_DestroyWindow(rec->win);
        int idx = (int)(rec - g_wins);
        g_wins[idx] = g_wins[--g_win_count];
    } else {
        SDL_DestroyWindow(win);
    }
    return 0;
}

EXPORT i32 zan_gui_is_maximized(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
}

/* 1 while the window can be seen (not minimized/hidden/occluded); ambient
 * animations pause while this reports 0. */
EXPORT i32 zan_gui_window_visible(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    SDL_WindowFlags f = SDL_GetWindowFlags(win);
    if (f & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))
        return 0;
    return 1;
}

/* 1 while the window has keyboard focus; ambient animations idle down to a
 * slow heartbeat while this reports 0, so a background IDE costs almost
 * nothing. */
EXPORT i32 zan_gui_window_focused(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0;
}

EXPORT i32 zan_gui_titlebar_height(void) { return g_titlebar_h; }
EXPORT i32 zan_gui_caption_button_width(void) { return g_btn_w; }

EXPORT i32 zan_gui_set_caption_buttons(iptr hwnd_val, i32 count) {
    if (count < 0 || count > 8) return 0;
    /* Record per-window so each window's hit-test excludes exactly its own
     * caption cluster; a dialog with fewer buttons no longer leaves the main
     * window's drag/close regions wrong. Keep the global as a create default. */
    g_caption_btn_count = (int)count;
    zan_sdl_win_t *rec = sdl_find((SDL_Window *)(intptr_t)hwnd_val);
    if (rec) rec->caption_btns = (int)count;
    return 0;
}

/* Move a window's top-left to (x, y) in screen pixels (designer "manual"
 * window position). */
EXPORT i32 zan_gui_set_window_pos(iptr hwnd_val, i32 x, i32 y) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 0;
    SDL_SetWindowPosition(win, (int)x, (int)y);
    return 1;
}

/* Re-center a window on the display it currently sits on. */
EXPORT i32 zan_gui_center_window(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 0;
    SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
    if (disp == 0) disp = SDL_GetPrimaryDisplay();
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(disp),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    return 1;
}

EXPORT i32 zan_gui_set_topmost(iptr hwnd_val, i32 on) {
    SDL_SetWindowAlwaysOnTop((SDL_Window *)(intptr_t)hwnd_val, on ? true : false);
    return 0;
}

#if defined(__ANDROID__)
/* Android's SDL_WaitEvent/WaitEventTimeout spin instead of blocking (their
 * event queue is pumped from the Java main thread), so a background-thread
 * app waiting on them burns a full core: the gallery sat at ~100% of one
 * CPU while idle. Instead, an event watch translates every SDL event into
 * the zan queue as it arrives (the Java thread posts input/lifecycle
 * events directly, so watches fire even while the SDL thread sleeps), and
 * the wait functions below block on the zan queue with real sleeps. */
static bool SDLCALL zan_event_watch(void *userdata, SDL_Event *e) {
    (void)userdata;
    if (!g_sdl_ready) return 1;
    /* Must translate quit-class events too: sdl_translate is what sets
     * g_quit. Skip only when the queue is full — they stay in SDL's queue
     * and a later drain picks them up. */
    if (zq_count() < ZAN_ZQ_CAP) sdl_translate(e);
    return 1; /* keep the event in SDL's queue; poll just empties it */
}

/* Block until an event lands in the zan queue (fed by zan_event_watch).
 * ms < 0: forever; 0: check once; >0: give up after ms. Returns 0 when
 * woken, -1 on timeout. Sleeps in <=16ms slices: watch-fed input never
 * needs sub-frame latency, and 60 light wakeups/s idle is ~0% CPU. */
static int zan_watch_wait(int ms) {
    Uint64 deadline = 0;
    if (ms > 0) deadline = SDL_GetTicks() + (Uint64)ms;
    for (;;) {
        if (!zq_empty()) return 0;
        if (ms == 0) return -1;
        Uint64 now = SDL_GetTicks();
        if (ms > 0 && now >= deadline) return -1;
        Uint64 left = ms > 0 ? deadline - now : (Uint64)0x7FFFFFFF;
        if (left > 0x7FFFFFFF) left = 0x7FFFFFFF;
        SDL_Delay((Uint32)(left > 16 ? 16 : left));
    }
}
#endif

/* Drain every SDL event currently available into the zan queue. Motion events
 * coalesce in zq_push, so this collapses a burst of samples to the single
 * latest position and, crucially, never leaves motion piling up in SDL's own
 * queue between frames (which is what starved the one-event-per-frame app). */
static void sdl_drain(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) sdl_translate(&e);
}

/* Android's SDL_WaitEvent/WaitEventTimeout spin instead of blocking (their
 * event queue was built for the Java main thread, which keeps pumping), so a
 * background-thread app waiting on them burns a full core: the gallery read
 * ~100% of one CPU while idle. zan_gui_wait_event* below therefore block on
 * the watch-fed zan queue (zan_watch_wait) instead of SDL's wait functions:
 * real idling when nothing happens, still correct when the app polls. */
EXPORT i32 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return 1;
#if defined(__ANDROID__)
    if (zan_watch_installed) {
        /* The watch translated everything as it arrived; translating the
         * polled copy again would enqueue every event twice. */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
    } else
#endif
    {
        sdl_drain();
    }
    if (zq_empty()) {
        i32 r = (g_quit) ? -1 : 1;
        fprof_ev("poll", r);
        return r;
    }
    zq_pop();
    fprof_ev("poll", 0);
    return 0;
}

EXPORT i32 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return -1;
    if (zq_empty()) {
#if defined(__ANDROID__)
        /* SDL_WaitEvent spins a full core on Android: sleep on the
         * watch-fed queue instead. */
        if (zan_watch_wait(-1) != 0) return -1;
#else
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) return -1;
        sdl_translate(&e);
        sdl_drain(); /* fold in anything already waiting behind it */
#endif
    }
    if (!zq_empty()) zq_pop(); /* else leaves kind 0 (e.g. a wake) */
    fprof_ev("wait", 0);
    return 0;
}

/* Like wait_event but gives up after `ms` milliseconds. Returns 0 when an
 * event was delivered, 1 on timeout, -1 on quit. */
EXPORT i32 zan_gui_wait_event_timeout(i32 ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) { fprof_ev("waitto", -1); return -1; }
    if (zq_empty()) {
#if defined(__ANDROID__)
        if (zan_watch_wait(ms) != 0) {
            i32 r = (g_quit) ? -1 : 1;
            fprof_ev("waitto", r);
            return r;
        }
#else
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, (Sint32)(ms < 0 ? 0 : ms))) {
            i32 r = (g_quit) ? -1 : 1;
            fprof_ev("waitto", r);
            return r;
        }
        sdl_translate(&e);
        sdl_drain();
#endif
    }
    if (zq_empty()) {
        i32 r = (g_quit) ? -1 : 1;
        fprof_ev("waitto", r);
        return r;
    }
    zq_pop();
    fprof_ev("waitto", 0);
    return 0;
}

EXPORT i32 zan_gui_wake(void) {
    if (!g_sdl_ready) return 0;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = g_wake_event;
    SDL_PushEvent(&ev);
    return 0;
}

/* Queue a synthetic input event for the automation driver (see Gui.UiDriver).
 * It is pushed onto the same internal zan event queue real SDL events feed, so
 * poll/wait_event report it exactly like OS input and the whole App + widget
 * dispatch path is exercised unchanged. Must be called on the UI thread (the
 * only thread that touches SDL events), which the driver is. */
EXPORT i32 zan_gui_inject_event(
    iptr hwnd_val, i32 kind, i32 x, i32 y, i32 button, i32 keycode, i32 mods) {
    SDL_Window *w = hwnd_val ? (SDL_Window *)(intptr_t)hwnd_val : g_main_win;
    zq_push((int)kind, (int)x, (int)y, (int)button, (int)keycode, (int)mods, w);
    /* Unblock a UI thread parked in wait_event so the event is served now. */
    if (g_sdl_ready) {
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = g_wake_event;
        SDL_PushEvent(&ev);
    }
    return 0;
}

/* Number of events still queued (synthetic + any un-popped real events). 0
 * means the driver's last injected batch has been fully consumed. */
EXPORT i32 zan_gui_inject_pending(void) {
    return (g_zq_tail - g_zq_head + ZAN_ZQ_CAP) % ZAN_ZQ_CAP;
}

EXPORT i32 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_seq(void)     { return g_ev_seq_sdl; }
EXPORT i32 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i32 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i32 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i32 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i32 zan_gui_event_mods(void)    { return g_pending_event[5]; }
/* Synthetic-marker channel (queue slot e[6]): 1 = this release ends a
 * touch drag, never a click. */
EXPORT i32 zan_gui_event_flag(void)    { return g_pending_event[6]; }

EXPORT i32 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i32 zan_gui_window_height(void) { return g_window_height; }

EXPORT iptr zan_gui_event_hwnd(void) { return (iptr)g_event_win; }

EXPORT i32 zan_gui_client_width(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return w;
}

EXPORT i32 zan_gui_client_height(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return h;
}

/* Lightweight opt-in frame profiler: enabled by env ZAN_FRAME_PROF=1 (desktop)
 * or, where the env can't be set (Android APKs), by the debug.zan.frame_prof
 * system property: 1 = 120-frame averages, 2 = one line per frame. Each line
 * breaks the frame budget into CPU-render / upload / present so we can target
 * the real bottleneck instead of guessing. Zero cost when off. */
static int g_fprof = -1;                 /* -1 = unread, 0 = off, 1 = 120-avg,
                                            2 = one line per frame */
static Uint64 g_fp_last_end = 0;         /* perf counter at previous present end */
static Uint64 g_fp_acc_interval = 0;     /* sum of gaps between successive frames */
static Uint64 g_fp_acc_upload = 0;
static Uint64 g_fp_acc_present = 0;
static int    g_fp_frames = 0;
/* Last upload, for the memory/perf report and the frame profiler: bytes
 * pushed into the texture and whether it was a whole-surface frame (empty
 * dirty list or overflow) or a damage-rect frame. Read by
 * zan_gui_mem_report's up= field and fprof's up_bytes=/full= fields. */
static size_t g_upload_last_bytes;     /* last SDL_UpdateTexture payload */
static int    g_upload_last_full;

/* APK processes fork from zygote, so ZAN_FRAME_PROF can't be set per-launch
 * there and its CWD is not writable either. debug.* props are settable by adb
 * shell on userdebug builds: `adb shell setprop debug.zan.frame_prof 1`
 * (120-frame averages) or 2 (one line per frame — interaction bursts live
 * only a few dozen frames). On Android the lines go to logcat (tag zan_fprof)
 * since /data/data is not readable without root; elsewhere to the file. */
static int fprof_android_prop(void) {
#if defined(__ANDROID__)
    char v[PROP_VALUE_MAX];
    memset(v, 0, sizeof v);
    __system_property_get("debug.zan.frame_prof", v);
    if (v[0] == '1') return 1;
    if (v[0] == '2') return 2;
#endif
    return 0;
}

static void fprof_emit(const char *line) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "zan_fprof", "%s", line);
#else
    FILE *fp = fopen("zan_frame_perf.log", "a");
    if (fp) {
        fputs(line, fp);
        fclose(fp);
    }
#endif
}

/* Temporary per-call trace of the fetch primitives (prop mode 2 only): lets a
 * scripted drag's logcat be replayed into an exact loop timeline — which wait
 * call woke the frame, with which event, how far ahead of the present. */
static void fprof_ev(const char *what, int ret) {
    if (g_fprof != 2) { return; }
    char line[128];
    snprintf(line, sizeof line, "t=%.1fms %s ret=%d kind=%d",
             (double)SDL_GetPerformanceCounter()
                 / (double)SDL_GetPerformanceFrequency() * 1000.0,
             what, ret, g_pending_event[0]);
    fprof_emit(line);
}

static void fprof_record(Uint64 t_enter, Uint64 t_upload, Uint64 t_present_end) {
    if (g_fprof < 0) {
        const char *e = getenv("ZAN_FRAME_PROF");
        g_fprof = (e && e[0] == '1') ? 1 : 0;
        if (!g_fprof) { g_fprof = fprof_android_prop(); }
    }
    if (!g_fprof) return;
    if (g_fp_last_end != 0) {
        /* interval = CPU render + event handling before this present; upload =
         * SDL_UpdateTexture; present = clear+blit+RenderPresent (incl. vsync). */
        double f = (double)SDL_GetPerformanceFrequency() / 1000.0; /* ticks per ms */
        double cpu = (double)(t_enter - g_fp_last_end) / f;
        double up  = (double)t_upload / f;
        double pr  = (double)(t_present_end - t_enter - t_upload) / f;
        char line[160];
        if (g_fprof == 2) {
            double f2 = (double)SDL_GetPerformanceFrequency() / 1000.0;
            /* g_last_event_at is 0 until the first event arrives: report
             * -1 instead of wrapping a unsigned difference around zero. */
            double gap = g_last_event_at
                ? (double)(t_enter - g_last_event_at) / f2 : -1.0;
            double after = (g_last_event_at && g_last_event_at > g_fp_last_end)
                ? (double)(g_last_event_at - g_fp_last_end) / f2 : -1.0;
            snprintf(line, sizeof line,
                     "frame cpu_render=%.2f upload=%.2f present=%.2f"
                     " up_bytes=%llu full=%d ev_gap=%.1f ev_after=%.1f evs=%d",
                     cpu, up, pr,
                     (unsigned long long)g_upload_last_bytes,
                     g_upload_last_full,
                     gap, after, g_ev_count);
            fprof_emit(line);
            g_ev_count = 0;
        } else {
            g_fp_acc_interval += t_enter - g_fp_last_end;
            g_fp_acc_upload   += t_upload;
            g_fp_acc_present  += t_present_end - t_enter - t_upload;
            g_fp_frames++;
            if (g_fp_frames >= 120) {
                double n = (double)g_fp_frames;
                cpu = (double)g_fp_acc_interval / n / f;
                up  = (double)g_fp_acc_upload   / n / f;
                pr  = (double)g_fp_acc_present  / n / f;
                double frame = cpu + up + pr;
                snprintf(line, sizeof line,
                         "frames=%d avg_frame=%.2fms fps=%.0f | cpu_render=%.2f upload=%.2f present=%.2f",
                         g_fp_frames, frame, frame > 0 ? 1000.0 / frame : 0.0, cpu, up, pr);
                fprof_emit(line);
                g_fp_acc_interval = 0; g_fp_acc_upload = 0;
                g_fp_acc_present = 0; g_fp_frames = 0;
            }
        }
    }
    g_fp_last_end = t_present_end;
}

/* Dirty rects for the next present: an animation frame that only repainted a
 * few hundred small particles has no reason to re-upload the whole surface
 * (several MB on a large window, and the single most expensive thing an idle
 * effect tick does). The caller announces the rects it touched; the present
 * uploads only those and then blits the texture as usual. The list is emptied
 * by every present, so a caller that announces nothing gets a full upload. */
#define ZAN_DIRTY_MAX 512
static SDL_Rect g_dirty[ZAN_DIRTY_MAX];
static int g_dirty_count;
static int g_dirty_overflow;

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_dirty_count >= ZAN_DIRTY_MAX) { g_dirty_overflow = 1; return 0; }
    g_dirty[g_dirty_count].x = (int)x;
    g_dirty[g_dirty_count].y = (int)y;
    g_dirty[g_dirty_count].w = (int)w;
    g_dirty[g_dirty_count].h = (int)h;
    g_dirty_count++;
    return 0;
}

/* ---- Game scene compositing --------------------------------------------
 *
 * A Game.* host owns the SDL window and paints the 3D/2D scene with its own
 * renderer; the Gui HUD layer lives on a normal zan_gui surface. Instead of
 * two windows, the game hands us its SDL_Window* and per-frame scene pixels:
 * a streaming texture holds the scene, zan_gui_scene_present composites
 * scene-below / surface-above in one renderer pass, so widgets get the full
 * Gui stack (CSS skins, layout, text engine) over the game.
 *
 * zan_gui_scene_upload hands over one BGRA frame (the game renders into a
 * locked staging buffer or reads back its target — Game.Zgm already keeps a
 * CPU-side frame for its replay/scrub path). Alpha is preserved, so the game
 * can also under-render the HUD area. */
typedef struct {
    SDL_Texture *tex;
    int tw, th;
} zan_scene_tex_t;
static zan_scene_tex_t g_scene;

static void sdl_upload(zan_surface_t *s, SDL_Texture *tex);

/* Renderer handed over by the game host (see zan_gui_scene_set_renderer). */
static SDL_Renderer *g_game_renderer = NULL;

EXPORT i32 zan_gui_adopt_sdl_window(iptr hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    zan_sdl_ensure_init();
    sdl_reclaim_closed();
    if (!g_sdl_ready || g_win_count >= ZAN_SDL_MAX_WIN) return 2;
    if (sdl_find(win)) return 0; /* already ours */
    if (g_game_renderer) {
        /* Reuse the renderer the game host handed over — a second SDL
         * renderer on the same window fails on most backends (D3D11 allows
         * exactly one swapchain per window). */
        zan_sdl_win_t *rec = &g_wins[g_win_count++];
        rec->win = win; rec->ren = g_game_renderer; rec->tex = NULL;
        rec->tw = rec->th = 0; rec->closed = 0;
        rec->caption_btns = g_caption_btn_count;
        if (!g_main_win) g_main_win = win;
        return 0;
    }
    /* No renderer handed over yet: create our own (pure-GUI windows). */
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) return 3;
    /* Same split as create_window: desktop unthrottled (app paces itself),
     * Android pinned to the display (see the comment there). */
#if defined(__ANDROID__)
    SDL_SetRenderVSync(ren, 1);
#else
    SDL_SetRenderVSync(ren, 0);
#endif
    SDL_SetWindowHitTest(win, zan_sdl_hittest, NULL);
    zan_sdl_win_t *rec2 = &g_wins[g_win_count++];
    rec2->win = win; rec2->ren = ren; rec2->tex = NULL;
    rec2->tw = rec2->th = 0; rec2->closed = 0;
    rec2->caption_btns = g_caption_btn_count;
    if (!g_main_win) g_main_win = win;
    return 0;
}

EXPORT i32 zan_gui_scene_set_renderer(iptr hwnd_val, iptr renderer_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    SDL_Renderer *ren = (SDL_Renderer *)(intptr_t)renderer_val;
    if (!ren) return 2;
    zan_sdl_ensure_init();
    sdl_reclaim_closed();
    if (!g_sdl_ready || g_win_count >= ZAN_SDL_MAX_WIN) return 3;
    zan_sdl_win_t *rec = sdl_find(win);
    if (rec) {
        if (rec->ren != ren) {
            /* Renderer changed under us: drop our streaming textures. */
            if (rec->tex) SDL_DestroyTexture(rec->tex);
            rec->tex = NULL; rec->tw = rec->th = 0;
            rec->ren = ren;
        }
    } else {
        rec = &g_wins[g_win_count++];
        rec->win = win; rec->ren = ren; rec->tex = NULL;
        rec->tw = rec->th = 0; rec->closed = 0;
        rec->caption_btns = g_caption_btn_count;
        if (!g_main_win) g_main_win = win;
    }
    g_game_renderer = ren;
    return 0;
}

EXPORT i32 zan_gui_scene_upload(iptr hwnd_val, const void *bgra, i32 w, i32 h) {
    (void)hwnd_val;
    if (!bgra || w <= 0 || h <= 0 || w > 8192 || h > 8192) return 1;
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    zan_sdl_win_t *rec = win ? sdl_find(win) : NULL;
    SDL_Renderer *ren = rec ? rec->ren : (g_main_win ? sdl_find(g_main_win) : NULL)->ren;
    if (!ren) return 2;
    if (!g_scene.tex || g_scene.tw != (int)w || g_scene.th != (int)h) {
        if (g_scene.tex) SDL_DestroyTexture(g_scene.tex);
        g_scene.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!g_scene.tex) { g_scene.tw = g_scene.th = 0; return 3; }
        SDL_SetTextureBlendMode(g_scene.tex, SDL_BLENDMODE_BLEND);
        g_scene.tw = (int)w; g_scene.th = (int)h;
    }
    SDL_UpdateTexture(g_scene.tex, NULL, bgra, (int)w * 4);
    return 0;
}

EXPORT i32 zan_gui_scene_present(iptr hwnd_val, i32 surface_id) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    zan_sdl_win_t *rec = win ? sdl_find(win) : NULL;
    if (!rec || !rec->ren) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 2;
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    if (!rec->tex || rec->tw != s->width || rec->th != s->height) {
        g_dirty_count = 0;
        g_dirty_overflow = 1;
        if (rec->tex) SDL_DestroyTexture(rec->tex);
        rec->tex = SDL_CreateTexture(rec->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     s->width, s->height);
        if (!rec->tex) return 3;
        SDL_SetTextureScaleMode(rec->tex, SDL_SCALEMODE_NEAREST);
        /* The HUD surface is cleared to alpha 0 and widgets paint straight
         * (non-premultiplied) alpha over it; without BLEND the default NONE
         * mode would ignore that alpha and the opaque windowbg background
         * painted by RenderBackground would cover the whole scene. */
        SDL_SetTextureBlendMode(rec->tex, SDL_BLENDMODE_BLEND);
        rec->tw = s->width; rec->th = s->height;
    }
    sdl_upload(s, rec->tex);
    g_dirty_count = 0;
    g_dirty_overflow = 0;

    /* Composite: scene first (stretched to the window — the game owns the
     * aspect), HUD surface above it 1:1. No background clear: the scene is
     * the background. */
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(rec->win, &ww, &wh);
    if (ww <= 0) SDL_GetWindowSize(rec->win, &ww, &wh);
    if (g_scene.tex) {
        SDL_FRect sdst = { 0.0f, 0.0f, (float)ww, (float)wh };
        SDL_RenderTexture(rec->ren, g_scene.tex, NULL, &sdst);
    } else {
        SDL_SetRenderDrawColor(rec->ren, (g_bg_color >> 16) & 0xFF,
                               (g_bg_color >> 8) & 0xFF, g_bg_color & 0xFF, 255);
        SDL_RenderClear(rec->ren);
    }
    SDL_FRect dst = { 0.0f, 0.0f, (float)s->width, (float)s->height };
    SDL_RenderTexture(rec->ren, rec->tex, NULL, &dst);
    SDL_RenderPresent(rec->ren);
    return 0;
}

static void sdl_upload(zan_surface_t *s, SDL_Texture *tex) {
    if (g_dirty_count == 0 || g_dirty_overflow) {
        SDL_UpdateTexture(tex, NULL, s->pixels, s->width * 4);
        g_upload_last_bytes = (size_t)s->width * (size_t)s->height
                              * sizeof(u32);
        g_upload_last_full = 1;
        return;
    }
    size_t uploaded = 0;
    for (int i = 0; i < g_dirty_count; i++) {
        SDL_Rect r = g_dirty[i];
        if (r.x < 0) { r.w += r.x; r.x = 0; }
        if (r.y < 0) { r.h += r.y; r.y = 0; }
        if (r.x + r.w > s->width)  r.w = s->width - r.x;
        if (r.y + r.h > s->height) r.h = s->height - r.y;
        if (r.w <= 0 || r.h <= 0) continue;
        const unsigned char *px = (const unsigned char *)s->pixels
            + (size_t)r.y * (size_t)s->width * 4 + (size_t)r.x * 4;
        SDL_UpdateTexture(tex, &r, px, s->width * 4);
        uploaded += (size_t)r.w * (size_t)r.h * sizeof(u32);
    }
    g_upload_last_bytes = uploaded;
    g_upload_last_full = 0;
}

EXPORT i32 zan_gui_present(iptr hwnd_val, i32 surface_id) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 1;
    zan_sdl_win_t *rec = sdl_find(win);
    if (!rec) return 1;
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    if (g_gl_reset_pending) {
        /* GL context was reset while backgrounded (Android): texture objects
         * survive but their contents are gone. Recreate on this (the app)
         * thread and let the fresh-texture path below force a FULL upload
         * from the retained CPU surface — the page repaints itself exactly
         * as it looked, and idle HUD frames stop patching a black texture. */
        g_gl_reset_pending = 0;
        for (int i = 0; i < g_win_count; i++) {
            zan_sdl_win_t *r = &g_wins[i];
            if (r->tex) SDL_DestroyTexture(r->tex);
            r->tex = NULL;
            r->tw = 0;
            r->th = 0;
        }
    }

    Uint64 t_enter = SDL_GetPerformanceCounter();

    if (!rec->tex || rec->tw != s->width || rec->th != s->height) {
        /* A fresh texture holds no previous frame to patch. */
        g_dirty_count = 0;
        g_dirty_overflow = 1;
        if (rec->tex) SDL_DestroyTexture(rec->tex);
        rec->tex = SDL_CreateTexture(rec->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     s->width, s->height);
        if (!rec->tex) return 1;
        SDL_SetTextureScaleMode(rec->tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(rec->tex, SDL_BLENDMODE_BLEND);
        rec->tw = s->width; rec->th = s->height;
    }
    sdl_upload(s, rec->tex);
    g_dirty_count = 0;
    g_dirty_overflow = 0;
    Uint64 t_upload = SDL_GetPerformanceCounter() - t_enter;

    /* Clear the (possibly larger) window to the canvas background so a live
     * grow-resize shows solid bg rather than stretched/garbage pixels, then
     * blit the frame 1:1 — never stretched. */
    SDL_SetRenderDrawColor(rec->ren, (g_bg_color >> 16) & 0xFF,
                           (g_bg_color >> 8) & 0xFF, g_bg_color & 0xFF, 255);
    SDL_RenderClear(rec->ren);
    SDL_FRect dst = { 0.0f, 0.0f, (float)s->width, (float)s->height };
    SDL_RenderTexture(rec->ren, rec->tex, NULL, &dst);
    SDL_RenderPresent(rec->ren);
    fprof_record(t_enter, t_upload, SDL_GetPerformanceCounter());
    return 0;
}

EXPORT i32 zan_gui_set_title(iptr hwnd_val, const char *title) {
    SDL_SetWindowTitle((SDL_Window *)(intptr_t)hwnd_val, title ? title : "");
    return 0;
}

EXPORT i32 zan_gui_set_cursor(i32 cursor_type) {
    static SDL_Cursor *cache[6];
    static int made = 0;
    if (!made) {
        /* Index order must match the Win32 zan_gui_set_cursor mapping, since
         * Gui/App feeds the same cursor id to both backends:
         * 0=arrow 1=hand(clickable) 2=I-beam(text) 3=EW 4=NS 5=crosshair.
         * (Previously 1/2 were TEXT/WAIT, so buttons showed an I-beam.) */
        cache[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cache[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        cache[2] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        cache[3] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        cache[4] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
        cache[5] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        made = 1;
    }
    /* The resize border is client area to the OS now (see g_resize_win), so
     * its cursor is ours to draw: override the app's per-frame choice while
     * the pointer sits on an edge, or for the whole manual drag. */
    int edge = g_resize_edge;
    if (!edge) {
        float mx = 0, my = 0;
        SDL_Window *w = SDL_GetMouseFocus();
        SDL_GetMouseState(&mx, &my);
        if (w) edge = zan_sdl_resize_edge(w, (int)mx, (int)my);
    }
    if (edge) {
        static SDL_Cursor *edges[4];
        static int emade = 0;
        if (!emade) {
            edges[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
            edges[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
            edges[2] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
            edges[3] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
            emade = 1;
        }
        SDL_SystemCursor want = zan_sdl_edge_cursor(edge);
        int i = 0;
        if (want == SDL_SYSTEM_CURSOR_NS_RESIZE) i = 1;
        else if (want == SDL_SYSTEM_CURSOR_NWSE_RESIZE) i = 2;
        else if (want == SDL_SYSTEM_CURSOR_NESW_RESIZE) i = 3;
        if (edges[i]) { SDL_SetCursor(edges[i]); return 0; }
    }
    int t = (int)cursor_type;
    if (t < 0 || t > 5) t = 0;
    if (cache[t]) SDL_SetCursor(cache[t]);
    return 0;
}

EXPORT i32 zan_gui_get_dpi_scale(void) { return (i32)(g_dpi * 100 / 96); }

/* Android-only: the app-private files directory (writable, per-user). SDL
 * owns the Activity, so the path comes from here rather than an env var;
 * stdlib FilePicker uses it as the default browsing root — the process cwd
 * is "/" and listing it is SELinux-denied for an untrusted app. */
EXPORT const char *zan_gui_android_files_dir(void) {
#if defined(__ANDROID__)
    return SDL_GetAndroidInternalStoragePath();
#else
    return "";
#endif
}

EXPORT i64 zan_gui_get_tick_ms(void) { return (i64)SDL_GetTicks(); }

EXPORT void zan_gui_sleep_ms(i32 ms) { if (ms > 0) SDL_Delay((Uint32)ms); }

EXPORT i32 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8) return -1;
    return SDL_SetClipboardText(utf8) ? 0 : -1;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    if (!SDL_HasClipboardText()) return "";
    char *t = SDL_GetClipboardText(); /* SDL-allocated, must SDL_free */
    if (!t) return "";
    size_t n = strlen(t);
    char *nb = (char *)malloc(n + 1);
    if (!nb) { SDL_free(t); return ""; }
    memcpy(nb, t, n + 1);
    SDL_free(t);
    free(g_clip_buf);
    g_clip_buf = nb;
    return g_clip_buf;
}

/* Non-destructive peek: 1 while paths from a completed drop remain
 * queued for zan_gui_drop_take. Kind 9 doubles as blur on some
 * backends, so apps use this to tell a real file drop apart. */
EXPORT int zan_gui_drop_pending(void) {
    return g_drop_head != g_drop_tail ? 1 : 0;
}

/* Pops the oldest path dropped on a window, or "" when the queue is empty.
 * The returned buffer stays valid until the next call. */
EXPORT const char *zan_gui_drop_take(void) {
    static char *g_drop_buf = NULL;
    if (g_drop_head == g_drop_tail) return "";
    char *p = g_drop_q[g_drop_head];
    g_drop_q[g_drop_head] = NULL;
    g_drop_head = (g_drop_head + 1) % ZAN_DROP_CAP;
    free(g_drop_buf);
    g_drop_buf = p;
    return g_drop_buf ? g_drop_buf : "";
}

EXPORT void zan_gui_set_ime_pos(i32 x, i32 y) {
    g_ime_x = (int)x; g_ime_y = (int)y;
    SDL_Window *win = g_event_win ? g_event_win : g_main_win;
    if (win) {
        SDL_Rect r = { (int)x, (int)y, 1, g_titlebar_h };
        SDL_SetTextInputArea(win, &r, 0);
    }
}

/* Open/close the text-input session. Android shows/hides the soft
 * keyboard with the session, so the UI layer drives it from text-editable
 * focus (FocusManager.UpdateImeSession). Desktop keeps the session open
 * for the window's lifetime; start/stop there is idempotent. */
EXPORT void zan_gui_set_ime_open(i32 on) {
    SDL_Window *win = g_event_win ? g_event_win : g_main_win;
    if (!win) { return; }
    if (on) { SDL_StartTextInput(win); } else { SDL_StopTextInput(win); }
}

/* Native OS glass has no portable SDL equivalent; the software frosted-glass
 * baked into the surface still renders. These stay as safe no-ops so themes
 * that toggle glass keep working under the unified backend. */
EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 0;
}
EXPORT i32 zan_gui_disable_glass(iptr hwnd_val) { (void)hwnd_val; return 0; }

/* Whole-window opacity, 10..100 percent. SDL composites this natively on
 * every platform (DWM / X11 compositor / Cocoa). */
EXPORT i32 zan_gui_set_opacity(iptr hwnd_val, i32 percent) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    SDL_SetWindowOpacity(win, (float)percent / 100.0f);
    return 0;
}

EXPORT i32 zan_gui_write_file(const char *path, const char *utf8) {
    if (!path || !utf8) return -1;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return -1;
    static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    SDL_WriteIO(io, bom, 3);
    size_t len = strlen(utf8);
    size_t wr = (len > 0) ? SDL_WriteIO(io, utf8, len) : 0;
    SDL_CloseIO(io);
    return (wr == len) ? 0 : -1;
}

#endif /* ZAN_GUI_SDL */
