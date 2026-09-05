/* gui_runtime_ohos.c -- HarmonyOS window/event/present shell (ZAN_GUI_OHOS).
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone. Plays the role gui_runtime_sdl.c
 * plays for SDL3 builds: owns the zan_gui_* window/event/present exports.
 *
 * One window = the XComponent surface the HAP shell attaches via
 * zan_gui_ohos_attach(); the HAP shell feeds touch through
 * zan_gui_ohos_touch(). Present goes through EGL: the composed CPU surface
 * is uploaded as one texture and drawn over the attached native window --
 * the same EGL path every OHOS app can rely on (the raw OH_NativeWindow_*
 * buffer API is not exported by all emulator images, so nothing here may
 * reference those symbols: they would fail the .so's load-time relocation
 * even when never called). Window management exports a desktop GUI needs
 * (minimize/maximize/titlebars/cursors/clipboard/drag-drop/glass) are
 * stubs: a phone app window has no such chrome. */

#ifdef ZAN_GUI_OHOS

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <native_window/external_window.h>

/* ---- NativeWindow direct path, resolved at runtime ----------------------
 * On real devices the fastest present writes the OHNativeWindow BufferQueue
 * directly (RequestBuffer/FlushBuffer, a few ms under EGL's swap on some
 * SoCs). Those symbols are not exported by every image -- the emulator
 * ships no libnative_window.so -- so they are looked up with dlsym instead
 * of linked: an undefined reference would fail the whole library's
 * load-time relocation even when the code path is never taken. The window
 * handle itself comes from the XComponent (the recommended source since
 * the CreateNativeWindow API deprecation). When the lookup fails
 * (emulator), present falls back to EGL below. */
typedef int (*FnNWRequestBuffer)(OHNativeWindow *, OHNativeWindowBuffer **, int *);
typedef int (*FnNWFlushBuffer)(OHNativeWindow *, OHNativeWindowBuffer *, int, Region *);
typedef BufferHandle *(*FnNWGetBufferHandle)(OHNativeWindowBuffer *);
typedef int (*FnNWHandleOpt)(OHNativeWindow *, int, ...);
static FnNWRequestBuffer   g_nw_request;
static FnNWFlushBuffer     g_nw_flush;
static FnNWGetBufferHandle g_nw_getbh;
static FnNWHandleOpt       g_nw_handleopt;

/* ---- window record -----------------------------------------------------
 * The HAP shell owns the real native window (from the XComponent); we only
 * ever hold the pointer it hands us. The Zan-facing hwnd is this record's
 * address, exactly like the SDL shell hands back SDL_Window*. */
typedef struct {
    void *nw;             /* NULL until the shell attaches the surface */
    int w, h;             /* attached surface size, device pixels */
    int attached;
    int closed;           /* Close() seen: present becomes a no-op */

    /* EGL present state; created on attach / first present, both on the
     * app thread that later calls zan_gui_present. */
    EGLDisplay egl_dpy;
    EGLSurface egl_surf;
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

/* ---- event ring --------------------------------------------------------
 * Same flat event protocol as the SDL shell: e[0] kind, e[1] x, e[2] y,
 * e[3] button, e[4] code (keycode / wheel delta), e[5] mods, e[6] flag.
 * Kinds: 1 move, 2 down, 3 up, 7 resize (x=w y=h), 8 close, 13 wheel.
 * The HAP shell pushes from the UI thread; the app thread polls. */
typedef struct { int e[8]; iptr win; } zan_oev_t;
#define ZAN_OQ_CAP 512
static zan_oev_t g_oq[ZAN_OQ_CAP];
static int g_oq_head = 0, g_oq_tail = 0;
static pthread_mutex_t g_oq_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_pending_event[8];
static iptr g_event_win = 0;
static long long g_ev_seq = 0;

/* Plain moves coalesce (freshest x/y wins) and wheel floods coalesce by
 * SUMMING deltas -- same contract as the SDL shell. The touch layer emits
 * one wheel per finger sample while the app eats one event per rendered
 * frame, so an un-coalesced wheel backlog lags the page visibly behind the
 * finger on slow-rendering pages (the gallery's glass theme runs ~1-6 fps
 * on the emulator). */
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

/* Same push, with the swallow-click flag set (e[6]): the app must not
 * treat this release as a click (drag end, or the pointer twin of a
 * sub-notch move). */
static void oq_push_flag_locked(int kind, int x, int y, int button, int code, int mods) {
    oq_push_locked(kind, x, y, button, code, mods);
    if (g_oq_head != g_oq_tail) {
        int last = (g_oq_tail + ZAN_OQ_CAP - 1) % ZAN_OQ_CAP;
        g_oq[last].e[6] = 1;
    }
}

/* ---- HAP-shell feed (called from the XComponent callbacks) ------------ */

EXPORT void zan_gui_ohos_attach(void *nw, int w, int h) {
    if (!nw || w <= 0 || h <= 0) return;
    pthread_mutex_lock(&g_oq_lock);
    g_owin.nw = nw;
    g_owin.w = w; g_owin.h = h;
    g_owin.attached = 1;
    g_window_width = w;
    g_window_height = h;
    oq_push_locked(7, w, h, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);

    /* Feature detection, once: direct BufferQueue on real devices, EGL
     * fallback where the NDK window library is absent (emulator). */
    if (!g_nw_request) {
        g_nw_request = (FnNWRequestBuffer)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowRequestBuffer");
        g_nw_flush = (FnNWFlushBuffer)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowFlushBuffer");
        g_nw_getbh = (FnNWGetBufferHandle)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_GetBufferHandleFromNative");
        g_nw_handleopt = (FnNWHandleOpt)dlsym(
            RTLD_DEFAULT, "OH_NativeWindow_NativeWindowHandleOpt");
        if (g_nw_handleopt && g_owin.nw) {
            int32_t fmt = 2; /* NATIVEBUFFER_PIXEL_FMT_BGRA_8888: what the
                              * software rasterizer's little-endian ARGB
                              * bytes already are */
            g_nw_handleopt((OHNativeWindow *)g_owin.nw, SET_FORMAT, fmt);
        }
    }
}

EXPORT void zan_gui_ohos_detach(void) {
    pthread_mutex_lock(&g_oq_lock);
    g_owin.attached = 0;
    g_owin.nw = NULL;
    oq_push_locked(8, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);
}

/* ---- touch gesture synthesis (port of the SDL shell's finger layer) -----
 * A phone has no wheel: the first finger's drag beyond an 8 px slop turns
 * into Win32-scale wheel events (kind 13) that move content 1:1 with the
 * finger, a tap under the slop is delivered as a full synthesized click at
 * the anchor, and finger-up with residual speed coasts (fling) through a
 * 16 ms thread until exponential friction eats it. The formulas are the
 * SDL shell's verbatim, so both phones scroll identically:
 *   wheel degrees = finger dy * 288 / dpi  (sub-degree remainder carries) */
static i64 ohs_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define OH_TOUCH_SLOP2   64.0f   /* (8 px)^2: below this, still a tap */
#define OH_TOUCH_HIST    8       /* release-velocity ring buffer */
#define OH_FLING_START_PX_S 250.0f
#define OH_FLING_STOP_PX_S  120.0f
#define OH_FLING_TAU_MS     400.0f

static int   g_tg_down, g_tg_drag;
static float g_tg_ax, g_tg_ay;    /* touch anchor */
static float g_tg_x, g_tg_y;      /* finger position */
static float g_tg_acc;            /* sub-notch wheel remainder */
static long long g_tg_ht[OH_TOUCH_HIST];
static float g_tg_hy[OH_TOUCH_HIST];
static int   g_tg_hn, g_tg_hi;

static int g_fling_active;
static float g_fling_v;           /* px/s, drag-sign convention */
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
        fling_cancel_locked(); /* a new touch always kills a coasting fling */
        g_tg_down = 1; g_tg_drag = 0; g_tg_acc = 0;
        g_tg_x = g_tg_ax = (float)x;
        g_tg_y = g_tg_ay = (float)y;
        g_tg_hn = 1; g_tg_hi = 1 % OH_TOUCH_HIST;
        g_tg_ht[0] = ohs_tick_ms(); g_tg_hy[0] = g_tg_y;
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
            /* A drag may start on a draggable widget (title bar, slider)
             * just as well as on scrollable content: synthesize the press
             * at the anchor so Gui-level hit testing sees it and the
             * widget can own the gesture, while the wheel stream below
             * keeps scrolling whatever the finger is over. */
            oq_push_locked(2, (int)g_tg_ax, (int)g_tg_ay, 0, 0, 0);
        }
        /* Finger travel -> wheel deltas in the ±120 scale Gui/App's /120
         * math expects; dragging up must scroll DOWN (content follows the
         * finger), i.e. negative delta. Sub-degree remainders accumulate
         * in g_tg_acc -- a per-event (int) cast there made slow drags
         * stall, then jump. */
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
        /* Sub-notch pointer twin: the wheel IS the motion when it carried
         * a delta (App updates mouseX/Y from kind 13 too); push a flagged
         * move only for sub-notch travel so held widgets still follow. */
        if (delta == 0) oq_push_flag_locked(1, x, y, 0, 0, 0);
        pthread_mutex_unlock(&g_oq_lock);
        return;
    }
    if (action == 2 && g_tg_down) {
        g_tg_down = 0;
        pthread_mutex_lock(&g_oq_lock);
        if (g_tg_drag) {
            /* Report the release at the finger position, flagged so the
             * app swallows it as a click: the finger lifted wherever the
             * drag ended, and treating that as a click would press
             * whatever sits under the release point. */
            oq_push_flag_locked(3, (int)g_tg_x, (int)g_tg_y, 0, 0, 0);
            pthread_mutex_unlock(&g_oq_lock);
            /* Release velocity from the recent travel window (oldest
             * sample still inside ~120 ms). */
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
            /* Tap: a full click at the anchor (move so hover/state is
             * right, then press + release). */
            int ax = (int)g_tg_ax, ay = (int)g_tg_ay;
            oq_push_locked(1, ax, ay, 0, 0, 0);
            oq_push_locked(2, ax, ay, 0, 0, 0);
            oq_push_locked(3, ax, ay, 0, 0, 0);
            pthread_mutex_unlock(&g_oq_lock);
        }
        return;
    }
}

/* ---- present (EGL) ------------------------------------------------------
 * The composed CPU surface goes up as one GL texture and out over the
 * attached native window through a fullscreen quad. Pixel bytes are B,G,R,A
 * (little-endian ARGB); they are uploaded as GL_RGBA and the fragment
 * shader swaps the channels back. */
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

static int ohos_gl_init(zan_ohos_win_t *w) {
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
    }
    if (!w->egl_ctx) {
        const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        w->egl_ctx = eglCreateContext(w->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
        if (w->egl_ctx == EGL_NO_CONTEXT) return 1;
    }
    if (!eglMakeCurrent(w->egl_dpy, w->egl_surf, w->egl_surf, w->egl_ctx))
        return 1;

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

static void ohos_texture(zan_ohos_win_t *w, const zan_surface_t *s) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, w->gl_tex);
    if (w->tex_w != s->width || w->tex_h != s->height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->width, s->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        w->tex_w = s->width;
        w->tex_h = s->height;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, s->stride);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                    GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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

    /* Fast path: real device with the NDK window library -- write the
     * BufferQueue buffer directly. */
    if (g_nw_request && g_nw_flush && g_nw_getbh) {
        OHNativeWindowBuffer *buf = NULL;
        int fence = -1;
        if (g_nw_request((OHNativeWindow *)g_owin.nw, &buf, &fence) != 0)
            goto egl_path;
        BufferHandle *bh = g_nw_getbh(buf);
        if (bh && bh->virAddr) {
            int bw = (int)bh->width;    /* buffer plane dims, pixels */
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
            region.rects = NULL;   /* header default: whole buffer dirty */
            region.rectNumber = 0;
            g_nw_flush((OHNativeWindow *)g_owin.nw, buf, -1, &region);
            return 0;
        }
    }
egl_path:
    /* Fallback: EGL texture upload + fullscreen quad. */
    if (!g_owin.gl_prog && ohos_gl_init(&g_owin) != 0) return 1;
    ohos_texture(&g_owin, s);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(g_owin.egl_dpy, g_owin.egl_surf);
    return 0;
}

/* Optional teardown the shell calls after zan_hap_main() returned. */
EXPORT void zan_gui_ohos_shutdown(void) {
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
    w->gl_prog = 0;
    w->tex_w = w->tex_h = 0;
}

EXPORT i32 zan_gui_present_dirty_add(i32 x, i32 y, i32 w, i32 h) {
    (void)x; (void)y; (void)w; (void)h;
    return 0; /* accepted; present copies the whole frame regardless */
}

/* ---- window management (phone: no chrome) ----------------------------- */

EXPORT iptr zan_gui_create_window(const char *title, i32 width, i32 height) {
    (void)title;
    (void)width; (void)height; /* the attached XComponent decides the size */
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

/* ---- event pump ------------------------------------------------------- */

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

/* Return contract mirrors the SDL driver -- the reference implementation the
 * Zan Window wrapper and every custom event loop (gallery, IDE) are written
 * against: poll 0 = event delivered / 1 = queue empty; wait 0 = delivered;
 * wait_event_timeout 0 = delivered / 1 = timed out. Inverting these starves
 * exactly the apps that escalate an empty poll into a timed wait, while
 * probes that read the event slots unconditionally keep working. */
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
/* UiDriver automation support: synthetic events go through the same ring as
 * real input, so hit testing, focus and click arbitration see them exactly
 * like a human's. The app may be parked in zan_gui_wait_event, which polls
 * every 4 ms -- no separate wake channel needed. */
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

/* ---- platform services ------------------------------------------------ */

EXPORT i32 zan_gui_get_dpi_scale(void) { return (i32)(g_dpi * 100 / 96); }

/* The HAP shell resolves the display density (libnative_display_manager lives
 * in the default linker namespace, out of reach of dlsym(RTLD_DEFAULT) from
 * this dlopened library) and hands it over before zan_hap_main. Without it
 * g_dpi stays at the 96 fallback and every control renders at desktop
 * physical size on a phone-class screen. */
EXPORT void zan_gui_ohos_set_dpi(i32 dpi) {
    if (dpi >= 48 && dpi <= 960) g_dpi = dpi;
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
EXPORT void zan_gui_set_ime_open(i32 on)           { (void)on; }
EXPORT i32 zan_gui_enable_glass(iptr hwnd_val, i32 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1; /* unsupported: app keeps CPU bg */
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

/* GameKit GPU-scene hooks: desktop-only, report unsupported. */
EXPORT i32 zan_gui_adopt_sdl_window(iptr hwnd_val)                  { (void)hwnd_val; return 1; }
EXPORT i32 zan_gui_scene_set_renderer(iptr hwnd_val, iptr rend)     { (void)hwnd_val; (void)rend; return 1; }
EXPORT i32 zan_gui_scene_upload(iptr hwnd_val, const void *bgra, i32 w, i32 h) {
    (void)hwnd_val; (void)bgra; (void)w; (void)h; return 1;
}
EXPORT i32 zan_gui_scene_present(iptr hwnd_val, i32 surface_id) {
    (void)hwnd_val; (void)surface_id; return 1;
}

#endif /* ZAN_GUI_OHOS */
