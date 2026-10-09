/* Getting a GL 3 */

#include "gui_gl.h"

#include <stddef.h>
#include <string.h>

int zan_gl_api_load(zan_gl_api *api, void *(*getproc)(const char *name)) {
    struct { const char *name; size_t off; } table[] = {
#define E(field, sym) { sym, offsetof(zan_gl_api, field) }
        E(Enable, "glEnable"),
        E(Disable, "glDisable"),
        E(BlendFunc, "glBlendFunc"),
        E(BlendFuncSeparate, "glBlendFuncSeparate"),
        E(Viewport, "glViewport"),
        E(Scissor, "glScissor"),
        E(ClearColor, "glClearColor"),
        E(Clear, "glClear"),
        E(PixelStorei, "glPixelStorei"),
        E(Flush, "glFlush"),
        E(Finish, "glFinish"),
        E(GetError, "glGetError"),
        E(GetString, "glGetString"),
        E(DrawArrays, "glDrawArrays"),
        E(DrawElements, "glDrawElements"),
        E(DepthFunc, "glDepthFunc"),
        E(ReadPixels, "glReadPixels"),
        E(CreateShader, "glCreateShader"),
        E(ShaderSource, "glShaderSource"),
        E(CompileShader, "glCompileShader"),
        E(GetShaderiv, "glGetShaderiv"),
        E(GetShaderInfoLog, "glGetShaderInfoLog"),
        E(DeleteShader, "glDeleteShader"),
        E(CreateProgram, "glCreateProgram"),
        E(AttachShader, "glAttachShader"),
        E(LinkProgram, "glLinkProgram"),
        E(GetProgramiv, "glGetProgramiv"),
        E(GetProgramInfoLog, "glGetProgramInfoLog"),
        E(UseProgram, "glUseProgram"),
        E(DeleteProgram, "glDeleteProgram"),
        E(GetUniformLocation, "glGetUniformLocation"),
        E(GetAttribLocation, "glGetAttribLocation"),
        E(Uniform1i, "glUniform1i"),
        E(Uniform2f, "glUniform2f"),
        E(Uniform4f, "glUniform4f"),
        E(UniformMatrix4fv, "glUniformMatrix4fv"),
        E(GenBuffers, "glGenBuffers"),
        E(DeleteBuffers, "glDeleteBuffers"),
        E(BindBuffer, "glBindBuffer"),
        E(BufferData, "glBufferData"),
        E(GenVertexArrays, "glGenVertexArrays"),
        E(DeleteVertexArrays, "glDeleteVertexArrays"),
        E(BindVertexArray, "glBindVertexArray"),
        E(EnableVertexAttribArray, "glEnableVertexAttribArray"),
        E(VertexAttribPointer, "glVertexAttribPointer"),
        E(GenTextures, "glGenTextures"),
        E(DeleteTextures, "glDeleteTextures"),
        E(BindTexture, "glBindTexture"),
        E(ActiveTexture, "glActiveTexture"),
        E(TexParameteri, "glTexParameteri"),
        E(TexImage2D, "glTexImage2D"),
        E(TexSubImage2D, "glTexSubImage2D"),
        E(GenFramebuffers, "glGenFramebuffers"),
        E(DeleteFramebuffers, "glDeleteFramebuffers"),
        E(BindFramebuffer, "glBindFramebuffer"),
        E(FramebufferTexture2D, "glFramebufferTexture2D"),
        E(GenRenderbuffers, "glGenRenderbuffers"),
        E(DeleteRenderbuffers, "glDeleteRenderbuffers"),
        E(BindRenderbuffer, "glBindRenderbuffer"),
        E(RenderbufferStorage, "glRenderbufferStorage"),
        E(FramebufferRenderbuffer, "glFramebufferRenderbuffer"),
        E(CheckFramebufferStatus, "glCheckFramebufferStatus"),
        E(BlitFramebuffer, "glBlitFramebuffer"),
#undef E
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        void *fn = getproc(table[i].name);
        if (!fn) return 0;
        *(void **)((char *)api + table[i].off) = fn;
    }
    return 1;
}

#ifdef _WIN32
/* Win32/WGL */
#include <windows.h>

#define WGL_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB       0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB        0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB    0x00000001

/* 内部辅助逻辑 */
typedef struct {
    HMODULE opengl32;
    HWND    wnd;
    HDC     dc;
    HGLRC   rc;
    int     pf;              /* pixel format the context was created for */
    HGLRC (WINAPI *CreateContext)(HDC);
    BOOL  (WINAPI *DeleteContext)(HGLRC);
    BOOL  (WINAPI *MakeCurrent)(HDC, HGLRC);
    BOOL  (WINAPI *SwapBuffers_)(HDC);
    PROC  (WINAPI *GetProcAddress_)(LPCSTR);
} zan_gl_ctx;

/* The GL child windows, one per shell window that presents through GL */
typedef struct {
    HWND host, child;
    HDC  dc;
    int  w, h;
} zan_gl_present_win;
static zan_gl_present_win g_glpres[8];

static void *zan_gl_ctx_getproc(const char *name);
static zan_gl_ctx g_glctx;

static void *zan_gl_ctx_getproc(const char *name) {
    /* 内部辅助逻辑 */
    PROC p = g_glctx.GetProcAddress_ ? g_glctx.GetProcAddress_(name) : NULL;
    if (p) return (void *)p;
    return (void *)GetProcAddress(g_glctx.opengl32, name);
}

static void zan_gl_ctx_present_drop_all(void);

static void zan_gl_ctx_destroy(void) {
    zan_gl_ctx_present_drop_all();
    if (g_glctx.rc && g_glctx.MakeCurrent && g_glctx.DeleteContext) {
        g_glctx.MakeCurrent(NULL, NULL);
        g_glctx.DeleteContext(g_glctx.rc);
    }
    if (g_glctx.dc) ReleaseDC(g_glctx.wnd, g_glctx.dc);
    if (g_glctx.wnd) DestroyWindow(g_glctx.wnd);
    if (g_glctx.opengl32) FreeLibrary(g_glctx.opengl32);
    memset(&g_glctx, 0, sizeof(g_glctx));
}

/* 内部辅助逻辑 */
static int zan_gl_ctx_create(void) {
    g_glctx.opengl32 = LoadLibraryA("opengl32.dll");
    if (!g_glctx.opengl32) return 0;
    g_glctx.CreateContext = (HGLRC (WINAPI *)(HDC))
        GetProcAddress(g_glctx.opengl32, "wglCreateContext");
    g_glctx.DeleteContext = (BOOL (WINAPI *)(HGLRC))
        GetProcAddress(g_glctx.opengl32, "wglDeleteContext");
    g_glctx.MakeCurrent = (BOOL (WINAPI *)(HDC, HGLRC))
        GetProcAddress(g_glctx.opengl32, "wglMakeCurrent");
    g_glctx.SwapBuffers_ = (BOOL (WINAPI *)(HDC))
        GetProcAddress(g_glctx.opengl32, "wglSwapBuffers");
    g_glctx.GetProcAddress_ = (PROC (WINAPI *)(LPCSTR))
        GetProcAddress(g_glctx.opengl32, "wglGetProcAddress");
    if (!g_glctx.CreateContext || !g_glctx.DeleteContext ||
        !g_glctx.MakeCurrent || !g_glctx.GetProcAddress_) {
        zan_gl_ctx_destroy();
        return 0;
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ZanGLBootstrap";
    RegisterClassA(&wc);
    g_glctx.wnd = CreateWindowA("ZanGLBootstrap", "", WS_OVERLAPPED,
                                0, 0, 8, 8, NULL, NULL, wc.hInstance, NULL);
    if (!g_glctx.wnd) { zan_gl_ctx_destroy(); return 0; }
    g_glctx.dc = GetDC(g_glctx.wnd);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    int pf = ChoosePixelFormat(g_glctx.dc, &pfd);
    if (!pf || !SetPixelFormat(g_glctx.dc, pf, &pfd)) {
        zan_gl_ctx_destroy(); return 0;
    }
    /* 内部辅助逻辑 */
    g_glctx.pf = pf;

    HGLRC boot = g_glctx.CreateContext(g_glctx.dc);
    if (!boot) { zan_gl_ctx_destroy(); return 0; }
    g_glctx.MakeCurrent(g_glctx.dc, boot);

    /* Ask for 3 */
    HGLRC (WINAPI *create_attribs)(HDC, HGLRC, const int *) =
        (HGLRC (WINAPI *)(HDC, HGLRC, const int *))
            g_glctx.GetProcAddress_("wglCreateContextAttribsARB");
    if (create_attribs) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        HGLRC core = create_attribs(g_glctx.dc, NULL, attribs);
        if (core) {
            g_glctx.MakeCurrent(NULL, NULL);
            g_glctx.DeleteContext(boot);
            g_glctx.rc = core;
            g_glctx.MakeCurrent(g_glctx.dc, core);
        }
    }
    if (!g_glctx.rc) {
        g_glctx.MakeCurrent(NULL, NULL);
        g_glctx.DeleteContext(boot);
        zan_gl_ctx_destroy();
        return 0;
    }
    return 1;
}

static int zan_gl_ctx_make_current(void) {
    if (!g_glctx.MakeCurrent) return 0;
    return g_glctx.MakeCurrent(g_glctx.dc, g_glctx.rc) ? 1 : 0;
}

/* 内部辅助逻辑 */
static LRESULT CALLBACK zan_gl_present_proc(HWND h, UINT msg, WPARAM wp,
                                           LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcA(h, msg, wp, lp);
}

static zan_gl_present_win *zan_gl_present_slot(HWND host) {
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++) if (g_glpres[i].host == host) return &g_glpres[i];
    for (int i = 0; i < n; i++) if (!g_glpres[i].host) return &g_glpres[i];
    return NULL;
}

static void zan_gl_ctx_present_drop(void *host) {
    zan_gl_present_win *p = NULL;
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++)
        if (g_glpres[i].host == (HWND)host) p = &g_glpres[i];
    if (!p) return;
    if (g_glctx.MakeCurrent) g_glctx.MakeCurrent(NULL, NULL);
    if (p->dc) ReleaseDC(p->child, p->dc);
    if (p->child) DestroyWindow(p->child);
    memset(p, 0, sizeof(*p));
    zan_gl_ctx_make_current();
}

static void zan_gl_ctx_present_drop_all(void) {
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++)
        if (g_glpres[i].host) zan_gl_ctx_present_drop((void *)g_glpres[i].host);
}

/* 内部辅助逻辑 */
static int zan_gl_ctx_present_begin(void *host, int w, int h) {
    HWND hw = (HWND)host;
    if (!hw || !g_glctx.rc || !g_glctx.pf || !g_glctx.SwapBuffers_) return 0;
    if (w <= 0 || h <= 0) return 0;
    zan_gl_present_win *p = zan_gl_present_slot(hw);
    if (!p) return 0;

    if (!p->child) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = zan_gl_present_proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "ZanGLPresent";
        /* 内部辅助逻辑 */
        wc.style = CS_OWNDC;
        RegisterClassA(&wc);
        HWND child = CreateWindowExA(0, "ZanGLPresent", "",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                     0, 0, w, h, hw, NULL, wc.hInstance, NULL);
        if (!child) return 0;
        HDC dc = GetDC(child);
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        if (!dc || !DescribePixelFormat(dc, g_glctx.pf, sizeof(pfd), &pfd) ||
            !SetPixelFormat(dc, g_glctx.pf, &pfd)) {
            if (dc) ReleaseDC(child, dc);
            DestroyWindow(child);
            return 0;
        }
        p->host = hw; p->child = child; p->dc = dc; p->w = w; p->h = h;
    } else if (p->w != w || p->h != h) {
        MoveWindow(p->child, 0, 0, w, h, FALSE);
        p->w = w; p->h = h;
    }

    if (!g_glctx.MakeCurrent(p->dc, g_glctx.rc)) {
        zan_gl_ctx_present_drop(hw);
        return 0;
    }
    return 1;
}

/* 内部辅助逻辑 */
static void zan_gl_ctx_present_end(void *host) {
    zan_gl_present_win *p = NULL;
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++)
        if (g_glpres[i].host == (HWND)host) p = &g_glpres[i];
    if (p && p->dc && g_glctx.SwapBuffers_) g_glctx.SwapBuffers_(p->dc);
    zan_gl_ctx_make_current();
}

#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
/* X11/GLX */
#include <dlfcn.h>
#include <X11/Xlib.h>

typedef void *GLXFBConfig_t;
typedef void *GLXContext_t;
typedef unsigned long GLXPbuffer_t;

#define GLX_RENDER_TYPE          0x8011
#define GLX_RGBA_BIT             0x00000001
#define GLX_DRAWABLE_TYPE        0x8010
#define GLX_PBUFFER_BIT          0x00000004
#define GLX_WINDOW_BIT           0x00000001
#define GLX_RED_SIZE             8
#define GLX_GREEN_SIZE           9
#define GLX_BLUE_SIZE            10
#define GLX_ALPHA_SIZE           11
#define GLX_DOUBLEBUFFER         5
#define GLX_PBUFFER_WIDTH        0x8041
#define GLX_PBUFFER_HEIGHT       0x8040
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB  0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef struct {
    void *libgl;
    Display *dpy;
    int owns_dpy;
    GLXPbuffer_t pbuf;
    GLXContext_t rc;
    GLXFBConfig_t cfg;       /* 内部辅助逻辑 */
    int can_present;         /* the config also does windows and double buffers */
    void *(*get_proc)(const char *);
    GLXFBConfig_t *(*choose_fbconfig)(Display *, int, const int *, int *);
    GLXPbuffer_t (*create_pbuffer)(Display *, GLXFBConfig_t, const int *);
    void (*destroy_pbuffer)(Display *, GLXPbuffer_t);
    GLXContext_t (*create_context_attribs)(Display *, GLXFBConfig_t,
                                           GLXContext_t, int, const int *);
    int (*make_context_current)(Display *, GLXPbuffer_t, GLXPbuffer_t,
                                GLXContext_t);
    void (*destroy_context)(Display *, GLXContext_t);
    XVisualInfo *(*get_visual)(Display *, GLXFBConfig_t);
    GLXPbuffer_t (*create_glxwindow)(Display *, GLXFBConfig_t, Window,
                                     const int *);
    void (*destroy_glxwindow)(Display *, GLXPbuffer_t);
    void (*swap_buffers)(Display *, GLXPbuffer_t);
} zan_gl_ctx;

static zan_gl_ctx g_glctx;

/* 内部辅助逻辑 */
typedef struct {
    Window       host, child;
    Colormap     cmap;
    GLXPbuffer_t glxwin;   /* a GLXWindow: same XID type as the pbuffer */
    int          w, h;
} zan_gl_present_win;
static zan_gl_present_win g_glpres[8];

/* 内部辅助逻辑 */
typedef void (*zan_anyfn)(void);
static zan_anyfn zan_gl_dlfn(void *lib, const char *name) {
    union { void *obj; zan_anyfn fn; } u;
    u.obj = dlsym(lib, name);
    return u.fn;
}

static void *zan_gl_ctx_getproc(const char *name) {
    void *p = NULL;
    if (g_glctx.get_proc) p = g_glctx.get_proc(name);
    if (!p) p = dlsym(g_glctx.libgl, name);
    return p;
}

/* Same lookup, typed as a function pointer for the GLX entry points below */
static zan_anyfn zan_gl_glxfn(const char *name) {
    union { void *obj; zan_anyfn fn; } u;
    u.obj = zan_gl_ctx_getproc(name);
    return u.fn;
}

static void zan_gl_ctx_present_drop_all(void);

static void zan_gl_ctx_destroy(void) {
    zan_gl_ctx_present_drop_all();
    if (g_glctx.rc && g_glctx.make_context_current)
        g_glctx.make_context_current(g_glctx.dpy, 0, 0, NULL);
    if (g_glctx.rc && g_glctx.destroy_context)
        g_glctx.destroy_context(g_glctx.dpy, g_glctx.rc);
    if (g_glctx.pbuf && g_glctx.destroy_pbuffer)
        g_glctx.destroy_pbuffer(g_glctx.dpy, g_glctx.pbuf);
    if (g_glctx.dpy && g_glctx.owns_dpy) XCloseDisplay(g_glctx.dpy);
    if (g_glctx.libgl) dlclose(g_glctx.libgl);
    memset(&g_glctx, 0, sizeof(g_glctx));
}

static int zan_gl_ctx_create(void) {
    g_glctx.libgl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!g_glctx.libgl) g_glctx.libgl = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
    if (!g_glctx.libgl) return 0;

    g_glctx.get_proc = (void *(*)(const char *))
        zan_gl_dlfn(g_glctx.libgl, "glXGetProcAddressARB");
    if (!g_glctx.get_proc) g_glctx.get_proc = (void *(*)(const char *))
        zan_gl_dlfn(g_glctx.libgl, "glXGetProcAddress");

#define ZGL_GLX(field, type, sym) g_glctx.field = (type)zan_gl_glxfn(sym)
    ZGL_GLX(choose_fbconfig,
            GLXFBConfig_t *(*)(Display *, int, const int *, int *),
            "glXChooseFBConfig");
    ZGL_GLX(create_pbuffer,
            GLXPbuffer_t (*)(Display *, GLXFBConfig_t, const int *),
            "glXCreatePbuffer");
    ZGL_GLX(destroy_pbuffer, void (*)(Display *, GLXPbuffer_t),
            "glXDestroyPbuffer");
    ZGL_GLX(create_context_attribs,
            GLXContext_t (*)(Display *, GLXFBConfig_t, GLXContext_t, int,
                             const int *),
            "glXCreateContextAttribsARB");
    ZGL_GLX(make_context_current,
            int (*)(Display *, GLXPbuffer_t, GLXPbuffer_t, GLXContext_t),
            "glXMakeContextCurrent");
    ZGL_GLX(destroy_context, void (*)(Display *, GLXContext_t),
            "glXDestroyContext");
    /* 内部辅助逻辑 */
    ZGL_GLX(get_visual, XVisualInfo *(*)(Display *, GLXFBConfig_t),
            "glXGetVisualFromFBConfig");
    ZGL_GLX(create_glxwindow,
            GLXPbuffer_t (*)(Display *, GLXFBConfig_t, Window, const int *),
            "glXCreateWindow");
    ZGL_GLX(destroy_glxwindow, void (*)(Display *, GLXPbuffer_t),
            "glXDestroyWindow");
    ZGL_GLX(swap_buffers, void (*)(Display *, GLXPbuffer_t),
            "glXSwapBuffers");
#undef ZGL_GLX
    if (!g_glctx.choose_fbconfig || !g_glctx.create_pbuffer ||
        !g_glctx.create_context_attribs || !g_glctx.make_context_current) {
        zan_gl_ctx_destroy();
        return 0;
    }

    /* 内部辅助逻辑 */
    g_glctx.dpy = g_display;
    if (!g_glctx.dpy) {
        g_glctx.dpy = XOpenDisplay(NULL);
        g_glctx.owns_dpy = 1;
    }
    if (!g_glctx.dpy) { zan_gl_ctx_destroy(); return 0; }

    /* 内部辅助逻辑 */
    const int present_attribs[] = {
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DOUBLEBUFFER, 1,
        0
    };
    const int offscreen_attribs[] = {
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DOUBLEBUFFER, 0,
        0
    };
    int n = 0;
    GLXFBConfig_t *cfgs = NULL;
    if (g_glctx.get_visual && g_glctx.create_glxwindow && g_glctx.swap_buffers) {
        cfgs = g_glctx.choose_fbconfig(g_glctx.dpy, DefaultScreen(g_glctx.dpy),
                                      present_attribs, &n);
        if (cfgs && n > 0) g_glctx.can_present = 1;
    }
    if (!g_glctx.can_present) {
        if (cfgs) XFree(cfgs);
        n = 0;
        cfgs = g_glctx.choose_fbconfig(g_glctx.dpy, DefaultScreen(g_glctx.dpy),
                                      offscreen_attribs, &n);
    }
    if (!cfgs || n <= 0) { zan_gl_ctx_destroy(); return 0; }
    g_glctx.cfg = cfgs[0];

    const int pb_attribs[] = {
        GLX_PBUFFER_WIDTH, 16, GLX_PBUFFER_HEIGHT, 16, 0
    };
    g_glctx.pbuf = g_glctx.create_pbuffer(g_glctx.dpy, cfgs[0], pb_attribs);
    const int ctx_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    g_glctx.rc = g_glctx.create_context_attribs(g_glctx.dpy, cfgs[0], NULL, 1,
                                                ctx_attribs);
    XFree(cfgs);
    if (!g_glctx.rc || !g_glctx.pbuf) { zan_gl_ctx_destroy(); return 0; }
    if (!g_glctx.make_context_current(g_glctx.dpy, g_glctx.pbuf, g_glctx.pbuf,
                                      g_glctx.rc)) {
        zan_gl_ctx_destroy();
        return 0;
    }
    return 1;
}

static int zan_gl_ctx_make_current(void) {
    return g_glctx.make_context_current(g_glctx.dpy, g_glctx.pbuf, g_glctx.pbuf,
                                        g_glctx.rc);
}

static zan_gl_present_win *zan_gl_present_find(Window host) {
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++) if (g_glpres[i].host == host) return &g_glpres[i];
    return NULL;
}

static void zan_gl_ctx_present_drop(void *host) {
    zan_gl_present_win *p = zan_gl_present_find((Window)(size_t)host);
    if (!p) return;
    g_glctx.make_context_current(g_glctx.dpy, 0, 0, NULL);
    if (p->glxwin && g_glctx.destroy_glxwindow)
        g_glctx.destroy_glxwindow(g_glctx.dpy, p->glxwin);
    if (p->child) XDestroyWindow(g_glctx.dpy, p->child);
    if (p->cmap) XFreeColormap(g_glctx.dpy, p->cmap);
    memset(p, 0, sizeof(*p));
    zan_gl_ctx_make_current();
}

/* 内部辅助逻辑 */
static int zan_gl_ctx_present_begin(void *host, int w, int h) {
    Window hw = (Window)(size_t)host;
    if (!hw || !g_glctx.rc || !g_glctx.can_present) return 0;
    if (w <= 0 || h <= 0) return 0;

    zan_gl_present_win *p = zan_gl_present_find(hw);
    if (!p) {
        int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
        for (int i = 0; i < n && !p; i++) if (!g_glpres[i].host) p = &g_glpres[i];
        if (!p) return 0;

        XVisualInfo *vi = g_glctx.get_visual(g_glctx.dpy, g_glctx.cfg);
        if (!vi) return 0;
        Colormap cmap = XCreateColormap(g_glctx.dpy, hw, vi->visual, AllocNone);
        XSetWindowAttributes swa;
        memset(&swa, 0, sizeof(swa));
        swa.colormap = cmap;
        swa.border_pixel = 0;
        /* 内部辅助逻辑 */
        swa.event_mask = 0;
        Window child = XCreateWindow(g_glctx.dpy, hw, 0, 0,
                                     (unsigned)w, (unsigned)h, 0, vi->depth,
                                     InputOutput, vi->visual,
                                     CWColormap | CWBorderPixel | CWEventMask,
                                     &swa);
        XFree(vi);
        if (!child) { XFreeColormap(g_glctx.dpy, cmap); return 0; }
        GLXPbuffer_t glxwin = g_glctx.create_glxwindow(g_glctx.dpy, g_glctx.cfg,
                                                      child, NULL);
        if (!glxwin) {
            XDestroyWindow(g_glctx.dpy, child);
            XFreeColormap(g_glctx.dpy, cmap);
            return 0;
        }
        XMapWindow(g_glctx.dpy, child);
        p->host = hw; p->child = child; p->glxwin = glxwin; p->cmap = cmap;
        p->w = w; p->h = h;
    } else if (p->w != w || p->h != h) {
        XResizeWindow(g_glctx.dpy, p->child, (unsigned)w, (unsigned)h);
        p->w = w; p->h = h;
    }

    if (!g_glctx.make_context_current(g_glctx.dpy, p->glxwin, p->glxwin,
                                     g_glctx.rc)) {
        zan_gl_ctx_present_drop(host);
        return 0;
    }
    return 1;
}

static void zan_gl_ctx_present_drop_all(void) {
    int n = (int)(sizeof(g_glpres) / sizeof(g_glpres[0]));
    for (int i = 0; i < n; i++)
        if (g_glpres[i].host)
            zan_gl_ctx_present_drop((void *)(size_t)g_glpres[i].host);
}

static void zan_gl_ctx_present_end(void *host) {
    zan_gl_present_win *p = zan_gl_present_find((Window)(size_t)host);
    if (p && p->glxwin && g_glctx.swap_buffers)
        g_glctx.swap_buffers(g_glctx.dpy, p->glxwin);
    zan_gl_ctx_make_current();
}

#elif defined(__APPLE__)
/* macOS/CGL */
#include <dlfcn.h>

/* 内部辅助逻辑 */
typedef void *CGLPixelFormatObj_t;
typedef void *CGLContextObj_t;

#define kCGLPFAOpenGLProfile     99
#define kCGLPFAAccelerated       73
#define kCGLPFAColorSize          8
#define kCGLPFAAlphaSize         11
#define kCGLOGLPVersion_GL4_Core 0x4100
#define kCGLOGLPVersion_3_2_Core 0x3200

typedef struct {
    void                *fw;
    CGLPixelFormatObj_t  pf;
    CGLContextObj_t      rc;
    int (*ChoosePixelFormat)(const int *, CGLPixelFormatObj_t *, int *);
    int (*DestroyPixelFormat)(CGLPixelFormatObj_t);
    int (*CreateContext)(CGLPixelFormatObj_t, CGLContextObj_t, CGLContextObj_t *);
    int (*DestroyContext)(CGLContextObj_t);
    int (*SetCurrentContext)(CGLContextObj_t);
} zan_gl_ctx;
static zan_gl_ctx g_glctx;

/* 内部辅助逻辑 */
typedef void (*zan_anyfn)(void);
static zan_anyfn zan_gl_cglfn(const char *name) {
    union { void *obj; zan_anyfn fn; } u;
    u.obj = g_glctx.fw ? dlsym(g_glctx.fw, name) : NULL;
    return u.fn;
}

static void *zan_gl_ctx_getproc(const char *name) {
    return g_glctx.fw ? dlsym(g_glctx.fw, name) : NULL;
}

static int zan_gl_ctx_make_current(void) {
    if (!g_glctx.rc || !g_glctx.SetCurrentContext) return 0;
    return g_glctx.SetCurrentContext(g_glctx.rc) == 0;
}

static void zan_gl_ctx_destroy(void) {
    if (g_glctx.SetCurrentContext) g_glctx.SetCurrentContext(NULL);
    if (g_glctx.rc && g_glctx.DestroyContext) g_glctx.DestroyContext(g_glctx.rc);
    if (g_glctx.pf && g_glctx.DestroyPixelFormat)
        g_glctx.DestroyPixelFormat(g_glctx.pf);
    if (g_glctx.fw) dlclose(g_glctx.fw);
    memset(&g_glctx, 0, sizeof(g_glctx));
}

static int zan_gl_ctx_create(void) {
    g_glctx.fw = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",
                        RTLD_LAZY | RTLD_LOCAL);
    if (!g_glctx.fw) return 0;
#define ZGL_CGL(field, type, sym) g_glctx.field = (type)zan_gl_cglfn(sym)
    ZGL_CGL(ChoosePixelFormat,
            int (*)(const int *, CGLPixelFormatObj_t *, int *),
            "CGLChoosePixelFormat");
    ZGL_CGL(DestroyPixelFormat, int (*)(CGLPixelFormatObj_t),
            "CGLDestroyPixelFormat");
    ZGL_CGL(CreateContext,
            int (*)(CGLPixelFormatObj_t, CGLContextObj_t, CGLContextObj_t *),
            "CGLCreateContext");
    ZGL_CGL(DestroyContext, int (*)(CGLContextObj_t), "CGLDestroyContext");
    ZGL_CGL(SetCurrentContext, int (*)(CGLContextObj_t),
            "CGLSetCurrentContext");
#undef ZGL_CGL
    if (!g_glctx.ChoosePixelFormat || !g_glctx.CreateContext ||
        !g_glctx.SetCurrentContext) { zan_gl_ctx_destroy(); return 0; }

    /* GL4 core first (GLSL 410 hardware), then the 3 */
    const int profiles[] = { kCGLOGLPVersion_GL4_Core, kCGLOGLPVersion_3_2_Core };
    for (int i = 0; i < 2 && !g_glctx.pf; i++) {
        const int attribs[] = {
            kCGLPFAOpenGLProfile, profiles[i],
            kCGLPFAAccelerated,
            kCGLPFAColorSize, 32,
            kCGLPFAAlphaSize, 8,
            0
        };
        int npix = 0;
        CGLPixelFormatObj_t pf = NULL;
        if (g_glctx.ChoosePixelFormat(attribs, &pf, &npix) == 0 && pf && npix > 0)
            g_glctx.pf = pf;
        else if (pf && g_glctx.DestroyPixelFormat)
            g_glctx.DestroyPixelFormat(pf);
    }
    if (!g_glctx.pf) { zan_gl_ctx_destroy(); return 0; }

    if (g_glctx.CreateContext(g_glctx.pf, NULL, &g_glctx.rc) != 0 || !g_glctx.rc) {
        g_glctx.rc = NULL;
        zan_gl_ctx_destroy();
        return 0;
    }
    if (!zan_gl_ctx_make_current()) { zan_gl_ctx_destroy(); return 0; }
    return 1;
}

/* 内部辅助逻辑 */
static int zan_gl_ctx_present_begin(void *host, int w, int h) {
    (void)host; (void)w; (void)h; return 0;
}
static void zan_gl_ctx_present_end(void *host) { (void)host; }
static void zan_gl_ctx_present_drop(void *host) { (void)host; }
static void zan_gl_ctx_present_drop_all(void) { }

#else
/* 内部辅助逻辑 */
static int zan_gl_ctx_create(void) { return 0; }
static int zan_gl_ctx_make_current(void) { return 0; }
static void zan_gl_ctx_destroy(void) { }
static void *zan_gl_ctx_getproc(const char *name) { (void)name; return NULL; }
static int zan_gl_ctx_present_begin(void *host, int w, int h) {
    (void)host; (void)w; (void)h; return 0;
}
static void zan_gl_ctx_present_end(void *host) { (void)host; }
static void zan_gl_ctx_present_drop(void *host) { (void)host; }
static void zan_gl_ctx_present_drop_all(void) { }
#endif
