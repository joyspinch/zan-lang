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
static int g_window_width = 0, g_window_height = 0; /* last-resized size (px) */
static int g_sdl_ready = 0;
static int g_quit = 0;
static Uint32 g_wake_event = 0; /* user event type posted by zan_gui_wake */
static int g_ime_x = 0, g_ime_y = 0;

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
    int next = (g_zq_tail + 1) % ZAN_ZQ_CAP;
    if (next == g_zq_head) return; /* full: drop oldest-safe (never overwrite) */
    zan_zev_t *z = &g_zq[g_zq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = 0; z->e[7] = 0;
    z->win = win;
    g_zq_tail = next;
}

/* Pop the front zan event into the reported slots. Caller checks !zq_empty. */
static void zq_pop(void) {
    zan_zev_t *z = &g_zq[g_zq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_zq_head = (g_zq_head + 1) % ZAN_ZQ_CAP;
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
            zq_push(1, (int)e->motion.x, (int)e->motion.y, 0, 0,
                    cur_mod_bits(), w);
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
                    SDL_Point p = { (int)e->button.x, (int)e->button.y };
                    if (w && zan_sdl_hittest(w, &p, NULL) == SDL_HITTEST_NORMAL) {
                        SDL_CaptureMouse(true);
                        g_mouse_captured = true;
                    }
                } else if (g_mouse_captured) {
                    SDL_CaptureMouse(false);
                    g_mouse_captured = false;
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
            sdl_represent(sdl_find(w));
            break;
        }
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
        case SDL_EVENT_KEY_DOWN: {
            SDL_Window *w = SDL_GetWindowFromID(e->key.windowID);
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

/* Borderless drag/resize regions — the SDL analogue of WM_NCHITTEST: an 8px
 * (DPI-scaled) resize border, a draggable caption strip minus the caption
 * button cluster on the right, everything else client. */
static SDL_HitTestResult SDLCALL zan_sdl_hittest(SDL_Window *win,
                                                 const SDL_Point *area,
                                                 void *data) {
    (void)data;
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    int x = area->x, y = area->y;
    int b = 8 * g_dpi / 96;
    int maxed = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
    zan_sdl_win_t *rec = sdl_find(win);
    int nbtn = rec ? rec->caption_btns : g_caption_btn_count;
    int inButtons = (y < g_titlebar_h &&
                     x >= W - nbtn * g_btn_w);
    if (!maxed && !inButtons) {
        int left = x < b, right = x >= W - b, top = y < b, bottom = y >= H - b;
        if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left)   return SDL_HITTEST_RESIZE_LEFT;
        if (right)  return SDL_HITTEST_RESIZE_RIGHT;
        if (top)    return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    if (inButtons) return SDL_HITTEST_NORMAL;
    if (y < g_titlebar_h) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

static void zan_sdl_ensure_init(void) {
    if (g_sdl_ready) return;
    SDL_SetMainReady();
    /* Deliver the click that raises/focuses a background window instead of
     * swallowing it, so a button clicked while the window is inactive fires on
     * the first click rather than needing a second (focus-then-click). */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) return;
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    g_dpi = (int)(scale * 96.0f + 0.5f);
    g_titlebar_h = 32 * g_dpi / 96;
    g_btn_w = 46 * g_dpi / 96;
    g_wake_event = SDL_RegisterEvents(1);
    g_sdl_ready = 1;
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
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
    /* App paces its own frames (needsRedraw + 16ms sleep); leave VSync off so a
     * present never blocks the UI thread up to a refresh interval. */
    SDL_SetRenderVSync(ren, 0);
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
    return (i64)(intptr_t)win;
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
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
    /* Route typed text (incl. IME commits) to this window. */
    SDL_StartTextInput(win);
    return 0;
}

EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    SDL_MinimizeWindow((SDL_Window *)(intptr_t)hwnd_val);
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(win);
    else SDL_MaximizeWindow(win);
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
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
EXPORT i64 zan_gui_destroy_window(i64 hwnd_val) {
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

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
}

/* 1 while the window can be seen (not minimized/hidden/occluded); ambient
 * animations pause while this reports 0. */
EXPORT i64 zan_gui_window_visible(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    SDL_WindowFlags f = SDL_GetWindowFlags(win);
    if (f & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))
        return 0;
    return 1;
}

EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w; }

EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    if (count < 0 || count > 8) return 0;
    /* Record per-window so each window's hit-test excludes exactly its own
     * caption cluster; a dialog with fewer buttons no longer leaves the main
     * window's drag/close regions wrong. Keep the global as a create default. */
    g_caption_btn_count = (int)count;
    zan_sdl_win_t *rec = sdl_find((SDL_Window *)(intptr_t)hwnd_val);
    if (rec) rec->caption_btns = (int)count;
    return 0;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    SDL_SetWindowAlwaysOnTop((SDL_Window *)(intptr_t)hwnd_val, on ? true : false);
    return 0;
}

/* Drain every SDL event currently available into the zan queue. Motion events
 * coalesce in zq_push, so this collapses a burst of samples to the single
 * latest position and, crucially, never leaves motion piling up in SDL's own
 * queue between frames (which is what starved the one-event-per-frame app). */
static void sdl_drain(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) sdl_translate(&e);
}

EXPORT i64 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return 1;
    sdl_drain();
    if (zq_empty()) return (g_quit) ? -1 : 1;
    zq_pop();
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return -1;
    if (zq_empty()) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) return -1;
        sdl_translate(&e);
        sdl_drain(); /* fold in anything already waiting behind it */
    }
    if (!zq_empty()) zq_pop(); /* else leaves kind 0 (e.g. a wake) */
    return 0;
}

/* Like wait_event but gives up after `ms` milliseconds. Returns 0 when an
 * event was delivered, 1 on timeout, -1 on quit. */
EXPORT i64 zan_gui_wait_event_timeout(i64 ms) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return -1;
    if (zq_empty()) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, (Sint32)(ms < 0 ? 0 : ms))) {
            return (g_quit) ? -1 : 1;
        }
        sdl_translate(&e);
        sdl_drain();
    }
    if (zq_empty()) return (g_quit) ? -1 : 1;
    zq_pop();
    return 0;
}

EXPORT i64 zan_gui_wake(void) {
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
EXPORT i64 zan_gui_inject_event(i64 hwnd_val, i64 kind, i64 x, i64 y,
                                i64 button, i64 keycode, i64 mods) {
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
EXPORT i64 zan_gui_inject_pending(void) {
    return (g_zq_tail - g_zq_head + ZAN_ZQ_CAP) % ZAN_ZQ_CAP;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i64 zan_gui_window_height(void) { return g_window_height; }

EXPORT i64 zan_gui_event_hwnd(void) { return (i64)(intptr_t)g_event_win; }

EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return w;
}

EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return h;
}

/* Lightweight opt-in frame profiler: enabled by setting env ZAN_FRAME_PROF=1.
 * Every 120 presents it appends one summary line to zan_frame_perf.log (in the
 * process CWD) breaking the frame budget into CPU-render / upload / present so
 * we can target the real bottleneck instead of guessing. Zero cost when off. */
static int g_fprof = -1;                 /* -1 = unread, 0 = off, 1 = on */
static Uint64 g_fp_last_end = 0;         /* perf counter at previous present end */
static Uint64 g_fp_acc_interval = 0;     /* sum of gaps between successive frames */
static Uint64 g_fp_acc_upload = 0;
static Uint64 g_fp_acc_present = 0;
static int    g_fp_frames = 0;

static void fprof_record(Uint64 t_enter, Uint64 t_upload, Uint64 t_present_end) {
    if (g_fprof < 0) {
        const char *e = getenv("ZAN_FRAME_PROF");
        g_fprof = (e && e[0] == '1') ? 1 : 0;
    }
    if (!g_fprof) return;
    if (g_fp_last_end != 0) {
        /* interval = CPU render + event handling before this present; upload =
         * SDL_UpdateTexture; present = clear+blit+RenderPresent (incl. vsync). */
        g_fp_acc_interval += t_enter - g_fp_last_end;
        g_fp_acc_upload   += t_upload;
        g_fp_acc_present  += t_present_end - t_enter - t_upload;
        g_fp_frames++;
        if (g_fp_frames >= 120) {
            double f = (double)SDL_GetPerformanceFrequency() / 1000.0; /* ticks per ms */
            double n = (double)g_fp_frames;
            double cpu = (double)g_fp_acc_interval / n / f;
            double up  = (double)g_fp_acc_upload   / n / f;
            double pr  = (double)g_fp_acc_present  / n / f;
            double frame = cpu + up + pr;
            FILE *fp = fopen("zan_frame_perf.log", "a");
            if (fp) {
                fprintf(fp, "frames=%d avg_frame=%.2fms fps=%.0f | cpu_render=%.2f upload=%.2f present=%.2f\n",
                        g_fp_frames, frame, frame > 0 ? 1000.0 / frame : 0.0, cpu, up, pr);
                fclose(fp);
            }
            g_fp_acc_interval = 0; g_fp_acc_upload = 0; g_fp_acc_present = 0; g_fp_frames = 0;
        }
    }
    g_fp_last_end = t_present_end;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 1;
    zan_sdl_win_t *rec = sdl_find(win);
    if (!rec) return 1;
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    Uint64 t_enter = SDL_GetPerformanceCounter();

    if (!rec->tex || rec->tw != s->width || rec->th != s->height) {
        if (rec->tex) SDL_DestroyTexture(rec->tex);
        rec->tex = SDL_CreateTexture(rec->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     s->width, s->height);
        if (!rec->tex) return 1;
        SDL_SetTextureScaleMode(rec->tex, SDL_SCALEMODE_NEAREST);
        rec->tw = s->width; rec->th = s->height;
    }
    SDL_UpdateTexture(rec->tex, NULL, s->pixels, s->width * 4);
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

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    SDL_SetWindowTitle((SDL_Window *)(intptr_t)hwnd_val, title ? title : "");
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
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
    int t = (int)cursor_type;
    if (t < 0 || t > 5) t = 0;
    if (cache[t]) SDL_SetCursor(cache[t]);
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) { return (i64)(g_dpi * 100 / 96); }

EXPORT i64 zan_gui_get_tick_ms(void) { return (i64)SDL_GetTicks(); }

EXPORT void zan_gui_sleep_ms(i64 ms) { if (ms > 0) SDL_Delay((Uint32)ms); }

EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
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

EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    g_ime_x = (int)x; g_ime_y = (int)y;
    SDL_Window *win = g_event_win ? g_event_win : g_main_win;
    if (win) {
        SDL_Rect r = { (int)x, (int)y, 1, g_titlebar_h };
        SDL_SetTextInputArea(win, &r, 0);
    }
}

/* Native OS glass has no portable SDL equivalent; the software frosted-glass
 * baked into the surface still renders. These stay as safe no-ops so themes
 * that toggle glass keep working under the unified backend. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 0;
}
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) { (void)hwnd_val; return 0; }

/* Whole-window opacity, 10..100 percent. SDL composites this natively on
 * every platform (DWM / X11 compositor / Cocoa). */
EXPORT i64 zan_gui_set_opacity(i64 hwnd_val, i64 percent) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    SDL_SetWindowOpacity(win, (float)percent / 100.0f);
    return 0;
}

EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
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
