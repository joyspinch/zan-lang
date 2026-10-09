/* gui_gl_backend */

#include "gui_gl.h"
#include "gui_gl_context.c"

/* plumbing */

static zan_gl_api gl;
/* 底层系统交互与数据协议契约 */
#define ZGL_MAX 0x8008
#define ZGL_FUNC_ADD 0x8006
static void (*zgl_blend_equation)(zgl_enum);
static int g_gl_state = 0;   /* 0 untried, 1 ready, -1 unusable */

#define ZGL_VF 34            /* 底层系统交互与数据协议契约 */

typedef enum {
    ZGL_MODE_NONE = 0,
    ZGL_MODE_BLEND,     /* shapes, source-over */
    ZGL_MODE_REPLACE,   /* 底层系统交互与数据协议契约 */
    ZGL_MODE_TEXT,      /* 核心系统底层抽象与内存语义契约 */
    ZGL_MODE_UNION      /* 模块核心语义抽象与接口调用契约 */
    /* 内部辅助逻辑 */
} zgl_mode;

/* 底层系统交互与数据协议契约 */
#define ZGL_K_RECT    0
#define ZGL_K_CIRCLE  1
#define ZGL_K_RADIAL  2
#define ZGL_K_SECTOR  3
#define ZGL_K_CAPSULE 4
#define ZGL_K_TEXT1   5   /* 底层系统交互与数据协议契约 */
#define ZGL_K_TEXT4   6   /* 底层系统交互与数据协议契约 */
#define ZGL_K_UNION   7   /* 核心系统底层抽象与内存语义契约 */
#define ZGL_K_TEXTRGBA 9 /* 底层系统交互与数据协议契约 */
#define ZGL_K_SURFACE 8   /* 核心系统底层抽象与内存语义契约 */
#define ZGL_K_BITMAP  10  /* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
#define ZGL_TILE 64

typedef struct {
    zgl_uint tex, fbo;
    zgl_uint cov_tex, cov_fbo; /* 核心系统底层抽象与内存语义契约 */
    int w, h;
    int gpu_ahead;   /* 底层系统交互与数据协议契约 */
    int cpu_ahead;   /* 模块核心语义抽象与接口调用契约 */
    /* 内部辅助逻辑 */
    unsigned char *shadow;
    int shadow_valid;
} zgl_target;

static zgl_target g_zgl_targets[64];

/* 内部辅助逻辑 */
static struct {
    zgl_uint prog_shape, prog_text, vao, vbo;
    zgl_int  u_viewport_shape, u_viewport_text, u_atlas1, u_atlas4, u_coverage,
             u_destination;
    zgl_uint atlas1, atlas4;         /* 核心系统底层抽象与内存语义契约 */
    zgl_int  u_atlas2;               /* 核心系统底层抽象与内存语义契约 */
    zgl_uint bitmap_tex;             /* 核心系统底层抽象与内存语义契约 */
    float   *verts;
    size_t   count, cap;             /* in floats */
    zgl_mode mode;
    zan_surface_t *target;
} g_zgl;

/* shaders */

static const char *ZGL_VS =
"#version 330 core\n"
"uniform vec2 uViewport;\n"
"in vec2 a_pos;\n"
"in vec2 a_center;\n"
"in vec4 a_shape;\n"   /* halfW, halfH, radius, stroke */
"in vec4 a_kind;\n"    /* kind, p0, p1, p2 */
"in vec4 a_col0;\n"
"in vec4 a_col1;\n"
"in vec4 a_col2;\n"
"in vec4 a_clip;\n"    /* x0, y0, x1, y1 (exclusive) */
"in vec4 a_seg;\n"     /* 核心系统底层抽象与内存语义契约 */
"in vec2 a_uv;\n"
"flat out vec2 v_center;\n"
"flat out vec4 v_shape;\n"
"flat out vec4 v_kind;\n"
"flat out vec4 v_col0;\n"
"flat out vec4 v_col1;\n"
"flat out vec4 v_col2;\n"
"flat out vec4 v_clip;\n"
"flat out vec4 v_seg;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    v_center = a_center; v_shape = a_shape; v_kind = a_kind;\n"
"    v_col0 = a_col0; v_col1 = a_col1; v_col2 = a_col2;\n"
"    v_clip = a_clip; v_seg = a_seg; v_uv = a_uv;\n"
/* 内部辅助逻辑 */
"    vec2 ndc = vec2(a_pos.x / uViewport.x * 2.0 - 1.0,\n"
"                    1.0 - a_pos.y / uViewport.y * 2.0);\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"}\n";

/* 内部辅助逻辑 */
#define ZGL_FS_COMMON \
"uniform vec2 uViewport;\n" \
"flat in vec2 v_center;\n" \
"flat in vec4 v_shape;\n" \
"flat in vec4 v_kind;\n" \
"flat in vec4 v_col0;\n" \
"flat in vec4 v_col1;\n" \
"flat in vec4 v_col2;\n" \
"flat in vec4 v_clip;\n" \
"flat in vec4 v_seg;\n" \
"in vec2 v_uv;\n" \
"vec2 zpix() { return vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y); }\n" \
"void zclip(vec2 p) {\n" \
"    if (p.x < v_clip.x || p.y < v_clip.y || p.x >= v_clip.z || p.y >= v_clip.w)\n" \
"        discard;\n" \
"}\n"

static const char *ZGL_FS_SHAPE =
"#version 330 core\n"
ZGL_FS_COMMON
"uniform sampler2D uCoverage;\n"
"uniform sampler2D uDestination;\n"
"uniform sampler2D uAtlas2;\n"
"vec4 over(vec4 src) {\n"
"    vec4 dst = texelFetch(uDestination, ivec2(gl_FragCoord.xy), 0);\n"
"    vec4 s = floor(clamp(src, 0.0, 1.0)*255.0 + 0.0001);\n"
"    vec4 d = floor(dst*255.0 + 0.5);\n"
"    if (s.a <= 0.0) return dst;\n"
"    float da = floor(d.a * (255.0-s.a) / 255.0);\n"
"    float a = s.a + da;\n"
"    /* 内部辅助逻辑 */\n"
"    return vec4(floor((s.rgb*s.a+d.rgb*da)/a), a)/255.0;\n"
"}\n"
"out vec4 o_color;\n"
/* 内部辅助逻辑 */
"float sd_round(vec2 p, vec2 half_, float r, int mask) {\n"
"    float rr = r;\n"
"    int bit = (p.x < 0.0) ? ((p.y < 0.0) ? 1 : 8) : ((p.y < 0.0) ? 2 : 4);\n"
"    if ((mask & bit) == 0) rr = 0.0;\n"
"    vec2 q = abs(p) - half_ + rr;\n"
"    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rr;\n"
"}\n"
"float sd_seg(vec2 p, vec2 a, vec2 b) {\n"
"    vec2 pa = p - a, ba = b - a;\n"
"    float t = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);\n"
"    return length(pa - ba * t);\n"
"}\n"
/* 内部辅助逻辑 */
"vec4 grad(float t) {\n"
"    if (v_kind.z > 0.5) {\n"
"        return (t < 0.5) ? mix(v_col0, v_col2, t * 2.0)\n"
"                         : mix(v_col2, v_col1, (t - 0.5) * 2.0);\n"
"    }\n"
"    return mix(v_col0, v_col1, t);\n"
"}\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
/* 底层系统交互与数据协议契约 */
"    vec2 lp = p - v_center;\n"
"    if (kind == 2) lp -= vec2(0.5);\n"
"    float cov = 0.0;\n"
"    vec4 col = v_col0;\n"
"    if (kind == 0) {\n"
"        float sd = sd_round(lp, v_shape.xy, v_shape.z, int(v_kind.y + 0.5));\n"
"        if (v_shape.w > 0.0) sd = abs(sd + v_shape.w * 0.5) - v_shape.w * 0.5;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
/* 内部辅助逻辑 */
"        int gdir = int(v_kind.w + 0.5);\n"
"        if (gdir >= 0) {\n"
"            vec2 h = v_shape.xy;\n"
"            float t;\n"
"            if (gdir == 1) t = (lp.x + h.x) / max(2.0 * h.x, 1.0);\n"
"            else if (gdir == 2) t = ((lp.x + h.x) + (lp.y + h.y))\n"
"                                    / max(2.0 * (h.x + h.y), 1.0);\n"
"            else if (gdir == 3) t = ((h.x - lp.x) + (lp.y + h.y))\n"
"                                    / max(2.0 * (h.x + h.y), 1.0);\n"
"            else t = (lp.y + h.y) / max(2.0 * h.y, 1.0);\n"
"            col = grad(clamp(t, 0.0, 1.0));\n"
"        }\n"
"    } else if (kind == 1) {\n"
/* 编译器代码生成与运行时系统底层调用契约 */
"        float sd = (v_shape.w > 0.0)\n"
"                 ? abs(length(lp) - v_shape.z) - v_shape.w * 0.5\n"
"                 : length(lp) - v_shape.z;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
/* 模块核心语义抽象与接口调用契约 */
"    } else if (kind == 2) {\n"
"        float r = v_shape.z;\n"
"        float d2 = dot(lp, lp);\n"
"        float t = (r * r - d2) / max(r * r, 1.0);\n"
"        if (t <= 0.0) discard;\n"
"        cov = t * t;\n"
"        col = vec4(v_col0.rgb, v_col0.a);\n"
"    } else if (kind == 3) {\n"
"        float dist = length(lp);\n"
"        float ri = v_kind.z, ro = v_shape.z;\n"
"        float radc = 1.0;\n"
"        if (dist > ro + 0.5) discard;\n"
"        if (ri > 0.0 && dist < ri - 0.5) discard;\n"
"        if (dist > ro - 0.5) radc = ro + 0.5 - dist;\n"
"        else if (ri > 0.0 && dist < ri + 0.5) radc = dist - (ri - 0.5);\n"
"        float a0 = v_kind.y, a1 = v_kind.w;\n"
/* 模块核心语义抽象与接口调用契约 */
"        float ang = degrees(atan(lp.x, -lp.y));\n"
"        if (ang < 0.0) ang += 360.0;\n"
"        float angc = 1.0;\n"
"        if (a1 - a0 < 360.0) {\n"
"            float dpix = (dist > 0.5) ? degrees(0.5 / dist) : 45.0;\n"
"            float aa = (ang < a0 - dpix) ? ang + 360.0 : ang;\n"
"            float lo = clamp((aa - a0 + dpix) / (2.0 * dpix), 0.0, 1.0);\n"
"            float hi = clamp((a1 - aa + dpix) / (2.0 * dpix), 0.0, 1.0);\n"
"            angc = lo * hi;\n"
"        }\n"
"        cov = clamp(radc, 0.0, 1.0) * angc;\n"
    "    } else if (kind == 4) {\n"
    "        float sd = sd_seg(p, v_seg.xy, v_seg.zw) - v_shape.z;\n"
    "        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
    "        if (v_kind.y > 0.5) cov = floor(cov * 255.0) / 255.0;\n"
    "    }\n"
    "    if (kind == 7) cov = texelFetch(uCoverage, ivec2(gl_FragCoord.xy), 0).r;\n"
"    if (kind == 8) {\n"
"        int mask = int(v_kind.y + 0.5);\n"
"        float outer = clamp(0.5 - sd_round(lp, v_shape.xy, v_shape.z, mask), 0.0, 1.0);\n"
"        float inner = outer;\n"
"        if (v_shape.w > 0.0) {\n"
"            vec2 h = v_shape.xy - v_shape.w;\n"
"            inner = (min(h.x, h.y) <= 0.0) ? 0.0 :\n"
"                clamp(0.5 - sd_round(lp, h, max(v_shape.z-v_shape.w, 0.0), mask), 0.0, outer);\n"
"        }\n"
"        float border = v_col1.a * (outer - inner);\n"
"        float fill = v_col0.a * (outer - border);\n"
"        float alpha = fill + border;\n"
"        if (alpha <= 0.0) discard;\n"
"        o_color = over(vec4((v_col0.rgb * fill + v_col1.rgb * border) / alpha, alpha));\n"
"        return;\n"
"    }\n"
/* 内部辅助逻辑 */
"    if (kind == 10) {\n"
"        col = texture(uAtlas2, v_uv) * v_col0;\n"
"        cov = 1.0;\n"
"    }\n"
"    if (cov <= 0.0) discard;\n"
"    o_color = (kind == 4 && v_kind.y > 0.5) ? vec4(cov)\n"
"                                          : vec4(col.rgb, col.a * cov);\n"
"    /* 内部辅助逻辑 */\n"
"    if (kind == 7 || kind == 8) o_color = over(o_color);\n"
"}\n";

/* 内部辅助逻辑 */
static const char *ZGL_FS_TEXT =
"#version 330 core\n"
ZGL_FS_COMMON
"uniform sampler2D uAtlas1;\n"
"uniform sampler2D uAtlas4;\n"
/* 内部辅助逻辑 */
"layout(location = 0, index = 0) out vec4 o_color;\n"
"layout(location = 0, index = 1) out vec4 o_cov;\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
"    if (kind == 9) {\n"
/* 内部辅助逻辑 */
"        vec4 t = texture(uAtlas4, v_uv);\n"
"        if (t.a <= 0.0) discard;\n"
"        o_color = vec4(t.rgb, 1.0);\n"
"        o_cov = vec4(t.a);\n"
"        return;\n"
"    }\n"
"    vec3 cov = (kind == 5) ? vec3(texture(uAtlas1, v_uv).r)\n"
"                           : texture(uAtlas4, v_uv).rgb;\n"
"    if (cov.r + cov.g + cov.b <= 0.0) discard;\n"
"    o_color = vec4(v_col0.rgb, 1.0);\n"
"    vec3 c = cov * v_col0.a;\n"
"    o_cov = vec4(c, max(max(c.r, c.g), c.b));\n"
"}\n";

/* GL bootstrap */

static zgl_uint zgl_compile(zgl_enum type, const char *src) {
    zgl_uint sh = gl.CreateShader(type);
    zgl_int len = (zgl_int)strlen(src);
    gl.ShaderSource(sh, 1, &src, &len);
    gl.CompileShader(sh);
    zgl_int ok = 0;
    gl.GetShaderiv(sh, ZGL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        zgl_sizei n = 0;
        gl.GetShaderInfoLog(sh, (zgl_sizei)sizeof(log), &n, log);
        fprintf(stderr, "[zan_gui] GL shader: %.*s\n", (int)n, log);
        gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static zgl_uint zgl_link(const char *vs_src, const char *fs_src) {
    zgl_uint vs = zgl_compile(ZGL_VERTEX_SHADER, vs_src);
    zgl_uint fs = zgl_compile(ZGL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    zgl_uint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    zgl_int ok = 0;
    gl.GetProgramiv(prog, ZGL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        zgl_sizei n = 0;
        gl.GetProgramInfoLog(prog, (zgl_sizei)sizeof(log), &n, log);
        fprintf(stderr, "[zan_gui] GL link: %.*s\n", (int)n, log);
        gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* 内部辅助逻辑 */
static void zgl_bind_attribs(zgl_uint prog) {
    struct { const char *name; int size; } a[] = {
        { "a_pos", 2 }, { "a_center", 2 }, { "a_shape", 4 }, { "a_kind", 4 },
        { "a_col0", 4 }, { "a_col1", 4 }, { "a_col2", 4 }, { "a_clip", 4 },
        { "a_seg", 4 }, { "a_uv", 2 },
    };
    size_t off = 0;
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++) {
        zgl_int loc = gl.GetAttribLocation(prog, a[i].name);
        if (loc >= 0) {
            gl.EnableVertexAttribArray((zgl_uint)loc);
            gl.VertexAttribPointer((zgl_uint)loc, a[i].size, ZGL_FLOAT,
                                   ZGL_FALSE, (zgl_sizei)(ZGL_VF * sizeof(float)),
                                   (const void *)(off * sizeof(float)));
        }
        off += (size_t)a[i].size;
    }
}

static zgl_uint zgl_new_atlas(zgl_enum internal, zgl_enum fmt, int dim) {
    zgl_uint t = 0;
    gl.GenTextures(1, &t);
    gl.BindTexture(ZGL_TEXTURE_2D, t);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, (zgl_int)internal, dim, dim, 0, fmt,
                  ZGL_UNSIGNED_BYTE, NULL);
    return t;
}

#define ZGL_ATLAS_DIM 1024

static int zgl_init(void) {
    if (g_gl_state) return g_gl_state > 0;
    g_gl_state = -1;
    if (!zan_gl_ctx_create()) return 0;
    /* 内部辅助逻辑 */
    if (!zan_gl_ctx_make_current()) { zan_gl_ctx_destroy(); return 0; }
    if (!zan_gl_api_load(&gl, zan_gl_ctx_getproc)) { zan_gl_ctx_destroy(); return 0; }
    zgl_blend_equation = (void (*)(zgl_enum))zan_gl_ctx_getproc("glBlendEquation");
    if (!zgl_blend_equation) { zan_gl_ctx_destroy(); return 0; }

    g_zgl.prog_shape = zgl_link(ZGL_VS, ZGL_FS_SHAPE);
    g_zgl.prog_text = zgl_link(ZGL_VS, ZGL_FS_TEXT);
    if (!g_zgl.prog_shape || !g_zgl.prog_text) { zan_gl_ctx_destroy(); return 0; }

    gl.GenVertexArrays(1, &g_zgl.vao);
    gl.GenBuffers(1, &g_zgl.vbo);
    gl.BindVertexArray(g_zgl.vao);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo);
    zgl_bind_attribs(g_zgl.prog_shape);

    g_zgl.u_viewport_shape = gl.GetUniformLocation(g_zgl.prog_shape, "uViewport");
    g_zgl.u_viewport_text = gl.GetUniformLocation(g_zgl.prog_text, "uViewport");
    g_zgl.u_coverage = gl.GetUniformLocation(g_zgl.prog_shape, "uCoverage");
    g_zgl.u_destination = gl.GetUniformLocation(g_zgl.prog_shape, "uDestination");
    g_zgl.u_atlas2 = gl.GetUniformLocation(g_zgl.prog_shape, "uAtlas2");
    g_zgl.u_atlas1 = gl.GetUniformLocation(g_zgl.prog_text, "uAtlas1");
    g_zgl.u_atlas4 = gl.GetUniformLocation(g_zgl.prog_text, "uAtlas4");

    gl.PixelStorei(ZGL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(ZGL_PACK_ALIGNMENT, 1);
    g_zgl.atlas1 = zgl_new_atlas(ZGL_R8, ZGL_RED, ZGL_ATLAS_DIM);
    g_zgl.atlas4 = zgl_new_atlas(ZGL_RGBA8, ZGL_BGRA, ZGL_ATLAS_DIM);

    g_zgl.cap = (size_t)ZGL_VF * 6 * 2048;
    g_zgl.verts = (float *)malloc(g_zgl.cap * sizeof(float));
    if (!g_zgl.verts) { zan_gl_ctx_destroy(); return 0; }

    if (gl.GetError() != ZGL_NO_ERROR) { zan_gl_ctx_destroy(); return 0; }
    g_gl_state = 1;
    return 1;
}

/* 核心系统底层抽象与内存语义契约 */

/* 模块核心语义抽象与接口调用契约 */
static zgl_target *zgl_target_of(zan_surface_t *s) {
    if (s->id < 0 || s->id >= (int)(sizeof(g_zgl_targets) / sizeof(g_zgl_targets[0])))
        return NULL;
    zgl_target *t = &g_zgl_targets[s->id];
    if (t->fbo && (t->w != s->width || t->h != s->height)) {
        gl.DeleteFramebuffers(1, &t->fbo);
        gl.DeleteTextures(1, &t->tex);
        if (t->cov_fbo) gl.DeleteFramebuffers(1, &t->cov_fbo);
        if (t->cov_tex) gl.DeleteTextures(1, &t->cov_tex);
        free(t->shadow);
        memset(t, 0, sizeof(*t));
    }
    if (!t->fbo) {
        gl.GenTextures(1, &t->tex);
        gl.BindTexture(ZGL_TEXTURE_2D, t->tex);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
        gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, s->width, s->height, 0,
                      ZGL_BGRA, ZGL_UNSIGNED_BYTE, NULL);
        gl.GenFramebuffers(1, &t->fbo);
        gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
        gl.FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                                ZGL_TEXTURE_2D, t->tex, 0);
        if (gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE) {
            gl.DeleteFramebuffers(1, &t->fbo);
            gl.DeleteTextures(1, &t->tex);
            memset(t, 0, sizeof(*t));
            return NULL;
        }
        t->w = s->width;
        t->h = s->height;
        t->shadow = (unsigned char *)malloc((size_t)s->width * (size_t)s->height * 4);
        /* 内部辅助逻辑 */
        t->cpu_ahead = 1;
    }
    return t;
}

static void zgl_flush(void);

/* 编译器代码生成与运行时系统底层调用契约 */
static void zgl_shadow_store(zan_surface_t *s, zgl_target *t,
                             int x, int y, int tw, int th) {
    size_t row = (size_t)tw * 4;
    for (int r = 0; r < th; r++)
        memcpy(t->shadow + ((size_t)(y + r) * (size_t)s->width + (size_t)x) * 4,
               s->pixels + (size_t)(y + r) * (size_t)s->stride + (size_t)x,
               row);
}

/* 编译器代码生成与运行时系统底层调用契约 */
static int zgl_tile_dirty(zan_surface_t *s, zgl_target *t,
                          int x, int y, int tw, int th) {
    size_t row = (size_t)tw * 4;
    for (int r = 0; r < th; r++) {
        if (memcmp(t->shadow + ((size_t)(y + r) * (size_t)s->width + (size_t)x) * 4,
                   s->pixels + (size_t)(y + r) * (size_t)s->stride + (size_t)x,
                   row) != 0)
            return 1;
    }
    return 0;
}

/* 编译器代码生成与运行时系统底层调用契约 */
static void zgl_upload(zan_surface_t *s, zgl_target *t) {
    if (!t->cpu_ahead) return;
    gl.BindTexture(ZGL_TEXTURE_2D, t->tex);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
    /* 编译器代码生成与运行时系统底层调用契约 */
    if (!t->shadow) {
        for (int y = 0; y < s->height; y++) {
            gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, 0, s->height - 1 - y,
                             s->width, 1, ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                             s->pixels + (size_t)y * (size_t)s->stride);
        }
        t->cpu_ahead = 0;
        return;
    }
    u32 tile[ZGL_TILE * ZGL_TILE];
    for (int y = 0; y < s->height; y += ZGL_TILE) {
        int th = s->height - y < ZGL_TILE ? s->height - y : ZGL_TILE;
        for (int x = 0; x < s->width; x += ZGL_TILE) {
            int tw = s->width - x < ZGL_TILE ? s->width - x : ZGL_TILE;
            if (t->shadow_valid && !zgl_tile_dirty(s, t, x, y, tw, th)) continue;
            for (int row = 0; row < th; row++)
                memcpy(tile + row * tw,
                       s->pixels + (size_t)(y + th - 1 - row) * s->stride + x,
                       (size_t)tw * sizeof(u32));
            gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, x, s->height - y - th, tw, th,
                             ZGL_BGRA, ZGL_UNSIGNED_BYTE, tile);
            zgl_shadow_store(s, t, x, y, tw, th);
        }
    }
    t->shadow_valid = 1;
    t->cpu_ahead = 0;
}

/* 内部辅助逻辑 */
static unsigned char *g_zgl_rb = NULL;
static size_t g_zgl_rb_cap = 0;

/* 内部辅助逻辑 */
static void zgl_readback(zan_surface_t *s, zgl_target *t) {
    if (!t->gpu_ahead) return;
    if (s->width <= 0 || s->height <= 0) return;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    /* 模块核心语义抽象与接口调用契约 */
    size_t row = (size_t)s->width * 4;
    size_t need = row * (size_t)s->height;
    if (need > g_zgl_rb_cap) {
        unsigned char *ng = (unsigned char *)realloc(g_zgl_rb, need);
        if (!ng) return;   /* 底层系统交互与数据协议契约 */
        g_zgl_rb = ng;
        g_zgl_rb_cap = need;
    }
    gl.ReadPixels(0, 0, s->width, s->height, ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                  g_zgl_rb);
    for (int y = 0; y < s->height; y++) {
        memcpy(s->pixels + (size_t)y * (size_t)s->stride,
               g_zgl_rb + row * (size_t)(s->height - 1 - y), row);
    }
    /* 内部辅助逻辑 */
    if (t->shadow) {
        zgl_shadow_store(s, t, 0, 0, s->width, s->height);
        t->shadow_valid = 1;
    }
    t->gpu_ahead = 0;
}

static void gl_sync_to_cpu(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    zgl_readback(s, t);
    t->cpu_ahead = 1;   /* 底层系统交互与数据协议契约 */
}

static void (*g_gpu_cleanup[8])(int32_t);
static int g_gpu_cleanup_count;
static uint64_t g_gpu_epoch = 1;

EXPORT i32 zan_gui_gpu_register_cleanup(void (*cleanup)(int32_t)) {
    if (!cleanup) return 0;
    for (int i = 0; i < g_gpu_cleanup_count; i++)
        if (g_gpu_cleanup[i] == cleanup) return 1;
    if (g_gpu_cleanup_count == 8) return 0;
    g_gpu_cleanup[g_gpu_cleanup_count++] = cleanup;
    return 1;
}

static void zgl_cleanup_extensions(int surface_id) {
    for (int i = 0; i < g_gpu_cleanup_count; i++)
        g_gpu_cleanup[i](surface_id);
}

static void gl_sync_from_cpu(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    if (g_zgl.target && g_zgl.target != s) zgl_flush();
    zgl_upload(s, t);
}

/* batching */

static void zgl_flush(void) {
    if (!g_zgl.count || !g_zgl.target) { g_zgl.count = 0; return; }
    zan_surface_t *s = g_zgl.target;
    zgl_target *t = zgl_target_of(s);
    if (!t) { g_zgl.count = 0; return; }

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    if (g_zgl.mode == ZGL_MODE_UNION)
        gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    zgl_blend_equation(g_zgl.mode == ZGL_MODE_UNION ? ZGL_MAX : ZGL_FUNC_ADD);
    gl.Viewport(0, 0, t->w, t->h);
    gl.BindVertexArray(g_zgl.vao);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo);
    gl.BufferData(ZGL_ARRAY_BUFFER,
                  (zgl_sizeiptr)(g_zgl.count * sizeof(float)),
                  g_zgl.verts, ZGL_STREAM_DRAW);
    /* 内部辅助逻辑 */
    int shader_over = 0;
    for (size_t v = 0; v + 8 <= g_zgl.count; v += ZGL_VF) {
        int k = (int)(g_zgl.verts[v + 8] + 0.5f);
        if (k == ZGL_K_UNION || k == ZGL_K_SURFACE) { shader_over = 1; break; }
    }
    static zgl_uint snap_tex, snap_fbo; static int snap_w, snap_h;

    if (g_zgl.mode == ZGL_MODE_TEXT) {
        gl.UseProgram(g_zgl.prog_text);
        zgl_bind_attribs(g_zgl.prog_text);
        gl.Uniform2f(g_zgl.u_viewport_text, (float)t->w, (float)t->h);
        gl.ActiveTexture(ZGL_TEXTURE0);
        gl.BindTexture(ZGL_TEXTURE_2D, g_zgl.atlas1);
        gl.Uniform1i(g_zgl.u_atlas1, 0);
        gl.ActiveTexture(ZGL_TEXTURE1);
        gl.BindTexture(ZGL_TEXTURE_2D, g_zgl.atlas4);
        gl.Uniform1i(g_zgl.u_atlas4, 1);
        gl.Enable(ZGL_BLEND);
        /* 内部辅助逻辑 */
        gl.BlendFuncSeparate(ZGL_SRC1_COLOR, ZGL_ONE_MINUS_SRC1_COLOR,
                             ZGL_SRC1_ALPHA, ZGL_ONE_MINUS_SRC1_ALPHA);
    } else {
        gl.UseProgram(g_zgl.prog_shape);
        zgl_bind_attribs(g_zgl.prog_shape);
        gl.Uniform2f(g_zgl.u_viewport_shape, (float)t->w, (float)t->h);
        gl.ActiveTexture(ZGL_TEXTURE0);
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (g_zgl.mode == ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, 0);
        } else if (shader_over) {
            /* 内部辅助逻辑 */
            if (snap_w != t->w || snap_h != t->h) {
                if (snap_tex) { gl.DeleteTextures(1, &snap_tex); gl.DeleteFramebuffers(1, &snap_fbo); snap_tex = snap_fbo = 0; }
                snap_w = snap_h = 0;
                gl.GenTextures(1, &snap_tex);
                gl.BindTexture(ZGL_TEXTURE_2D, snap_tex);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
                gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, t->w, t->h, 0,
                              ZGL_BGRA, ZGL_UNSIGNED_BYTE, NULL);
                gl.GenFramebuffers(1, &snap_fbo);
                gl.BindFramebuffer(ZGL_FRAMEBUFFER, snap_fbo);
                gl.FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                                        ZGL_TEXTURE_2D, snap_tex, 0);
                if (gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE) {
                    gl.DeleteFramebuffers(1, &snap_fbo); gl.DeleteTextures(1, &snap_tex);
                    snap_tex = snap_fbo = 0; snap_w = snap_h = 0;
                } else snap_w = t->w, snap_h = t->h;
            } else {
                gl.BindTexture(ZGL_TEXTURE_2D, snap_tex);
            }
            if (snap_tex) {
                gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->fbo);
                gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, snap_fbo);
                gl.BlitFramebuffer(0, 0, t->w, t->h, 0, 0, t->w, t->h,
                                   ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);
                gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
                gl.ActiveTexture(ZGL_TEXTURE1);
                gl.BindTexture(ZGL_TEXTURE_2D, snap_tex);
                gl.Uniform1i(g_zgl.u_destination, 1);
                gl.ActiveTexture(ZGL_TEXTURE0);
            } else {
                /* 内部辅助逻辑 */
                shader_over = 0;
            }
        }
        /* 模块核心语义抽象与接口调用契约 */
        if (g_zgl.mode != ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, t->cov_tex);
            gl.Uniform1i(g_zgl.u_coverage, 0);
        }
        /* 内部辅助逻辑 */
        if (g_zgl.bitmap_tex) {
            gl.ActiveTexture(ZGL_TEXTURE2);
            gl.BindTexture(ZGL_TEXTURE_2D, g_zgl.bitmap_tex);
            gl.Uniform1i(g_zgl.u_atlas2, 2);
            gl.ActiveTexture(ZGL_TEXTURE0);
        }
        if (g_zgl.mode == ZGL_MODE_REPLACE || shader_over) {
            gl.Disable(ZGL_BLEND);
        } else {
            gl.Enable(ZGL_BLEND);
            /* 内部辅助逻辑 */
            gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                 ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        }
    }

    gl.DrawArrays(ZGL_TRIANGLES, 0, (zgl_sizei)(g_zgl.count / ZGL_VF));
    g_zgl.count = 0;
    if (g_zgl.mode != ZGL_MODE_UNION) t->gpu_ahead = 1;
    zgl_blend_equation(ZGL_FUNC_ADD);
    /* 内部辅助逻辑 */
    gl.Enable(ZGL_BLEND);
}

/* 内部辅助逻辑 */
static int zgl_begin(zan_surface_t *s, zgl_mode mode) {
    if (g_gl_state <= 0) return 0;
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    if (g_zgl.target != s || g_zgl.mode != mode) {
        zgl_flush();
        g_zgl.target = s;
        g_zgl.mode = mode;
    }
    zgl_upload(s, t);
    return 1;
}

typedef struct {
    float cx, cy;              /* 核心系统底层抽象与内存语义契约 */
    float hw, hh;              /* 核心系统底层抽象与内存语义契约 */
    float radius, stroke;
    int   kind;
    float p0, p1, p2;          /* kind-specific */
    float col0[4], col1[4], col2[4];
    float seg[4];
    float u0, v0, u1, v1;      /* 核心系统底层抽象与内存语义契约 */
} zgl_quad;

static void zgl_color(float *out, u32 c, int force_opaque) {
    out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(c & 0xFF) / 255.0f;
    u32 a = (c >> 24) & 0xFF;
    out[3] = force_opaque ? 1.0f : (float)a / 255.0f;
}

/* 内部辅助逻辑 */
static void zgl_push(zan_surface_t *s, const zgl_quad *q,
                     float x0, float y0, float x1, float y1) {
    /* 内部辅助逻辑 */
    float cx0 = (float)s->clip_x0, cy0 = (float)s->clip_y0;
    float cx1 = (float)s->clip_x1, cy1 = (float)s->clip_y1;
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x1 <= x0 || y1 <= y0) return;

    if (g_zgl.count + (size_t)ZGL_VF * 6 > g_zgl.cap) {
        size_t cap = g_zgl.cap * 2;
        float *grown = (float *)realloc(g_zgl.verts, cap * sizeof(float));
        if (grown) { g_zgl.verts = grown; g_zgl.cap = cap; }
        else { zgl_flush(); if (g_zgl.count + (size_t)ZGL_VF * 6 > g_zgl.cap) return; }
    }

    const float corner[6][2] = {
        { x0, y0 }, { x1, y0 }, { x1, y1 },
        { x0, y0 }, { x1, y1 }, { x0, y1 },
    };
    /* 内部辅助逻辑 */
    float du = (q->u1 - q->u0), dv = (q->v1 - q->v0);
    float qw = (x1 - x0), qh = (y1 - y0);
    (void)qw; (void)qh;
    for (int i = 0; i < 6; i++) {
        float *v = g_zgl.verts + g_zgl.count;
        float px = corner[i][0], py = corner[i][1];
        v[0] = px;            v[1] = py;
        v[2] = q->cx;         v[3] = q->cy;
        v[4] = q->hw;         v[5] = q->hh;
        v[6] = q->radius;     v[7] = q->stroke;
        v[8] = (float)q->kind; v[9] = q->p0; v[10] = q->p1; v[11] = q->p2;
        memcpy(v + 12, q->col0, 4 * sizeof(float));
        memcpy(v + 16, q->col1, 4 * sizeof(float));
        memcpy(v + 20, q->col2, 4 * sizeof(float));
        v[24] = (float)s->clip_x0; v[25] = (float)s->clip_y0;
        v[26] = (float)s->clip_x1; v[27] = (float)s->clip_y1;
        memcpy(v + 28, q->seg, 4 * sizeof(float));
        /* 编译器代码生成与运行时系统底层调用契约 */
        float fu = (q->hw > 0.0f) ? (px - (q->cx - q->hw)) / (2.0f * q->hw) : 0.0f;
        float fv = (q->hh > 0.0f) ? (py - (q->cy - q->hh)) / (2.0f * q->hh) : 0.0f;
        v[32] = q->u0 + du * fu;
        v[33] = q->v0 + dv * fv;
        g_zgl.count += ZGL_VF;
    }
}

/* primitives */

static void zgl_rect_quad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, int stroke, int gdir,
                          u32 c0, u32 c1, u32 via, int opaque) {
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_RECT;
    q.cx = (float)x + (float)w / 2.0f;
    q.cy = (float)y + (float)h / 2.0f;
    q.hw = (float)w / 2.0f;
    q.hh = (float)h / 2.0f;
    int r = radius;
    if (r < 0) r = 0;
    if (r > 0 && (corners & 15) == 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    q.radius = (float)r;
    q.stroke = (float)stroke;
    q.p0 = (float)(corners & 15);
    q.p1 = (via != 0) ? 1.0f : 0.0f;
    q.p2 = (float)gdir;
    zgl_color(q.col0, c0, opaque);
    zgl_color(q.col1, c1, opaque);
    zgl_color(q.col2, via, opaque);
    zgl_push(s, &q, (float)x, (float)y, (float)(x + w), (float)(y + h));
}

static void gl_clear_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (!zgl_begin(s, ZGL_MODE_REPLACE)) { cpu_clear_rect(s, x, y, w, h, c); return; }
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
}

static void gl_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (((c >> 24) & 0xFF) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) { cpu_fill_rect(s, x, y, w, h, c); return; }
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
}

static void gl_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 c) {
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_round(s, x, y, w, h, radius, corners, c); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, -1, c, c, 0, 0);
}

static void gl_draw_round(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 c, int thickness) {
    if (thickness <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_round(s, x, y, w, h, radius, corners, c, thickness); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, thickness, -1, c, c, 0, 0);
}

static void gl_surface_round(zan_surface_t *s, int x, int y, int w, int h,
                             int radius, int corners, u32 fill, u32 border, int thickness) {
    if (w <= 0 || h <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_surface_round(s, x, y, w, h, radius, corners, fill, border, thickness);
        return;
    }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_SURFACE;
    q.cx = (float)x + (float)w * 0.5f;
    q.cy = (float)y + (float)h * 0.5f;
    q.hw = (float)w * 0.5f; q.hh = (float)h * 0.5f;
    int r = radius < 0 ? 0 : radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    q.radius = (float)r;
    q.stroke = thickness > 0 ? (float)thickness : 0.0f;
    q.p0 = (float)(corners & 15);
    zgl_color(q.col0, fill, 0); zgl_color(q.col1, border, 0);
    zgl_push(s, &q, (float)x, (float)y, (float)x + (float)w, (float)y + (float)h);
    zgl_flush(); /* 内部辅助逻辑 */
}

static void gl_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 top, u32 bottom) {
    /* 内部辅助逻辑 */
    int rounded = (radius > 0 && (corners & 15) != 0);
    if (!zgl_begin(s, rounded ? ZGL_MODE_BLEND : ZGL_MODE_REPLACE)) {
        cpu_fill_vgrad(s, x, y, w, h, radius, corners, top, bottom); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, ZAN_GRAD_VERTICAL,
                  top, bottom, 0, 1);
}

static void gl_fill_grad(zan_surface_t *s, int x, int y, int w, int h,
                         int radius, int corners, int dir,
                         u32 from, u32 via, u32 to) {
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_grad(s, x, y, w, h, radius, corners, dir, from, via, to);
        return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, dir, from, to, via, 0);
}

static void gl_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                      int stroke) {
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_CIRCLE;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)radius;
    q.stroke = (float)stroke;
    /* 模块核心语义抽象与接口调用契约 */
    q.hw = (float)radius + (float)stroke * 0.5f + 2.0f;
    q.hh = q.hw;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_fill_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c) {
    if (radius <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_circle(s, cx, cy, radius, c); return;
    }
    gl_circle(s, cx, cy, radius, c, 0);
}

static void gl_draw_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                           int thickness) {
    if (radius <= 0 || thickness <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_circle(s, cx, cy, radius, c, thickness); return;
    }
    gl_circle(s, cx, cy, radius, c, thickness);
}

static void gl_fill_radial(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                           int inner_alpha) {
    if (radius <= 0 || inner_alpha <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_radial(s, cx, cy, radius, c, inner_alpha); return;
    }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_RADIAL;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)radius;
    q.hw = (float)radius + 1.0f;
    q.hh = (float)radius + 1.0f;
    zgl_color(q.col0, (c & 0x00FFFFFFu) |
              ((u32)(inner_alpha > 255 ? 255 : inner_alpha) << 24), 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                           int r_outer, int a0_deg, int a1_deg, u32 c) {
    if (r_outer <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_sector(s, cx, cy, r_inner, r_outer, a0_deg, a1_deg, c); return;
    }
    int a0 = a0_deg, a1 = a1_deg;
    if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_SECTOR;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)r_outer;
    q.p0 = (float)a0;
    q.p1 = (float)r_inner;
    q.p2 = (float)a1;
    q.hw = (float)r_outer + 2.0f;
    q.hh = (float)r_outer + 2.0f;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_capsule(zan_surface_t *s, float x0, float y0, float x1, float y1,
                       float half, u32 c) {
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_CAPSULE;
    q.p0 = g_zgl.mode == ZGL_MODE_UNION ? 1.0f : 0.0f;
    q.radius = half;
    q.seg[0] = x0; q.seg[1] = y0; q.seg[2] = x1; q.seg[3] = y1;
    float lo_x = (x0 < x1 ? x0 : x1) - half - 1.0f;
    float hi_x = (x0 > x1 ? x0 : x1) + half + 1.0f;
    float lo_y = (y0 < y1 ? y0 : y1) - half - 1.0f;
    float hi_y = (y0 > y1 ? y0 : y1) + half + 1.0f;
    q.cx = (lo_x + hi_x) / 2.0f;
    q.cy = (lo_y + hi_y) / 2.0f;
    q.hw = (hi_x - lo_x) / 2.0f;
    q.hh = (hi_y - lo_y) / 2.0f;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
}

static void gl_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                         u32 c, int thickness) {
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_line(s, x0, y0, x1, y1, c, thickness); return;
    }
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    /* 模块核心语义抽象与接口调用契约 */
    float offset = thickness > 1 ? 0.0f : 0.5f;
    gl_capsule(s, (float)x0 + offset, (float)y0 + offset,
               (float)x1 + offset, (float)y1 + offset, half, c);
}

/* 核心系统底层抽象与内存语义契约 */
static int zgl_union_target(zgl_target *t) {
    if (t->cov_fbo) return 1;
    gl.ActiveTexture(ZGL_TEXTURE0);
    gl.GenTextures(1, &t->cov_tex);
    gl.BindTexture(ZGL_TEXTURE_2D, t->cov_tex);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_R8, t->w, t->h, 0,
                  ZGL_RED, ZGL_UNSIGNED_BYTE, NULL);
    gl.GenFramebuffers(1, &t->cov_fbo);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                            ZGL_TEXTURE_2D, t->cov_tex, 0);
    int ok = t->cov_tex && t->cov_fbo &&
             gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) == ZGL_FRAMEBUFFER_COMPLETE;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    if (!ok) {
        gl.DeleteFramebuffers(1, &t->cov_fbo);
        gl.DeleteTextures(1, &t->cov_tex);
        t->cov_fbo = t->cov_tex = 0;
    }
    return ok;
}

static void gl_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                        int thickness, int fx) {
    if (!pts || n < 2 || (c >> 24) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_polyline(s, pts, n, c, thickness, fx);
        return;
    }
    /* 编译器代码生成与运行时系统底层调用契约 */
    float scale = fx == 0 ? 1.0f : 1.0f / 256.0f;
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    float lo_x = (float)s->width, lo_y = (float)s->height, hi_x = 0, hi_y = 0;
    int segments = 0;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale, ay = (float)pts[i * 2 + 1] * scale;
        float bx = (float)pts[i * 2 + 2] * scale, by = (float)pts[i * 2 + 3] * scale;
        float dx = bx - ax, dy = by - ay;
        if (dx * dx + dy * dy < 0.0001f) continue;
        segments++;
        float pad = half + 1.0f;
        lo_x = fminf(lo_x, fminf(ax, bx) - pad);
        lo_y = fminf(lo_y, fminf(ay, by) - pad);
        hi_x = fmaxf(hi_x, fmaxf(ax, bx) + pad);
        hi_y = fmaxf(hi_y, fmaxf(ay, by) + pad);
    }
    lo_x = fmaxf(lo_x, fmaxf(0.0f, (float)s->clip_x0));
    lo_y = fmaxf(lo_y, fmaxf(0.0f, (float)s->clip_y0));
    hi_x = fminf(hi_x, fminf((float)s->width, (float)s->clip_x1));
    hi_y = fminf(hi_y, fminf((float)s->height, (float)s->clip_y1));
    if (!segments || lo_x >= hi_x || lo_y >= hi_y) return;
    int clear_x = (int)floorf(lo_x), clear_y = (int)floorf(lo_y);
    int clear_x1 = (int)ceilf(hi_x), clear_y1 = (int)ceilf(hi_y);
    zgl_flush(); /* 底层系统交互与数据协议契约 */
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        /* 底层系统交互与数据协议契约 */
        fprintf(stderr, "[zan_gui] GL polyline coverage framebuffer unavailable; CPU fallback\n");
        gl_sync_to_cpu(s);
        cpu_polyline(s, pts, n, c, thickness, fx);
        return;
    }
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(clear_x, t->h - clear_y1, clear_x1 - clear_x, clear_y1 - clear_y);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT);
    gl.Disable(ZGL_SCISSOR_TEST); /* 核心系统底层抽象与内存语义契约 */
    g_zgl.mode = ZGL_MODE_UNION;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale;
        float ay = (float)pts[i * 2 + 1] * scale;
        float bx = (float)pts[i * 2 + 2] * scale;
        float by = (float)pts[i * 2 + 3] * scale;
        float dx = bx - ax, dy = by - ay;
        if (dx * dx + dy * dy < 0.0001f) continue; /* 核心系统底层抽象与内存语义契约 */
        gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
    }
    zgl_flush(); /* 底层系统交互与数据协议契约 */
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush(); /* 底层系统交互与数据协议契约 */
}

/* 编译器代码生成与运行时系统底层调用契约 */
static void cpu_polybatch_loop(zan_surface_t *s, const int32_t *pts,
                               const int32_t *counts, int n_paths, u32 c,
                               int thickness, int fx) {
    int off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        if (n >= 2) cpu_polyline(s, pts + off, n, c, thickness, fx);
        off += n * 2;
    }
}

/* 内部辅助逻辑 */
static void gl_polybatch(zan_surface_t *s, const int32_t *pts,
                         const int32_t *counts, int n_paths, u32 c,
                         int thickness, int fx) {
    if (!pts || !counts || n_paths < 1 || (c >> 24) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_polybatch_loop(s, pts, counts, n_paths, c, thickness, fx);
        return;
    }
    float scale = fx == 0 ? 1.0f : 1.0f / 256.0f;
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    float lo_x = (float)s->width, lo_y = (float)s->height, hi_x = 0, hi_y = 0;
    int segments = 0;
    int off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        for (int i = 0; n >= 2 && i + 1 < n; i++) {
            float ax = (float)pts[off + i * 2] * scale;
            float ay = (float)pts[off + i * 2 + 1] * scale;
            float bx = (float)pts[off + i * 2 + 2] * scale;
            float by = (float)pts[off + i * 2 + 3] * scale;
            float dx = bx - ax, dy = by - ay;
            if (dx * dx + dy * dy < 0.0001f) continue;
            segments++;
            float pad = half + 1.0f;
            lo_x = fminf(lo_x, fminf(ax, bx) - pad);
            lo_y = fminf(lo_y, fminf(ay, by) - pad);
            hi_x = fmaxf(hi_x, fmaxf(ax, bx) + pad);
            hi_y = fmaxf(hi_y, fmaxf(ay, by) + pad);
        }
        off += n * 2;
    }
    lo_x = fmaxf(lo_x, fmaxf(0.0f, (float)s->clip_x0));
    lo_y = fmaxf(lo_y, fmaxf(0.0f, (float)s->clip_y0));
    hi_x = fminf(hi_x, fminf((float)s->width, (float)s->clip_x1));
    hi_y = fminf(hi_y, fminf((float)s->height, (float)s->clip_y1));
    if (!segments || lo_x >= hi_x || lo_y >= hi_y) return;
    int clear_x = (int)floorf(lo_x), clear_y = (int)floorf(lo_y);
    int clear_x1 = (int)ceilf(hi_x), clear_y1 = (int)ceilf(hi_y);
    zgl_flush(); /* 底层系统交互与数据协议契约 */
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        /* 底层系统交互与数据协议契约 */
        fprintf(stderr, "[zan_gui] GL polybatch coverage framebuffer unavailable; CPU fallback\n");
        gl_sync_to_cpu(s);
        cpu_polybatch_loop(s, pts, counts, n_paths, c, thickness, fx);
        return;
    }
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(clear_x, t->h - clear_y1, clear_x1 - clear_x, clear_y1 - clear_y);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT);
    gl.Disable(ZGL_SCISSOR_TEST); /* 核心系统底层抽象与内存语义契约 */
    g_zgl.mode = ZGL_MODE_UNION;
    off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        for (int i = 0; n >= 2 && i + 1 < n; i++) {
            float ax = (float)pts[off + i * 2] * scale;
            float ay = (float)pts[off + i * 2 + 1] * scale;
            float bx = (float)pts[off + i * 2 + 2] * scale;
            float by = (float)pts[off + i * 2 + 3] * scale;
            float dx = bx - ax, dy = by - ay;
            if (dx * dx + dy * dy < 0.0001f) continue; /* 核心系统底层抽象与内存语义契约 */
            gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
        }
        off += n * 2;
    }
    zgl_flush(); /* 底层系统交互与数据协议契约 */
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush(); /* 底层系统交互与数据协议契约 */
}

/* text */

/* 底层系统交互与数据协议契约 */
typedef struct {
    uint32_t id, rev;
    int x, y, w, h;
    int bpp;
} zgl_tile_slot;

#define ZGL_TILES 4096
static zgl_tile_slot g_zgl_tiles[ZGL_TILES];
static struct { int x, y, row_h; } g_zgl_shelf[2];   /* [0] = R8, [1] = RGBA8 */

static void zgl_atlas_reset(void) {
    memset(g_zgl_tiles, 0, sizeof(g_zgl_tiles));
    memset(g_zgl_shelf, 0, sizeof(g_zgl_shelf));
}

static zgl_tile_slot *zgl_tile_upload(const zan_glyph_tile *tile) {
    if (!tile || tile->w <= 0 || tile->h <= 0 || !tile->cov) return NULL;
    if (tile->w > ZGL_ATLAS_DIM || tile->h > ZGL_ATLAS_DIM) return NULL;
    zgl_tile_slot *slot = &g_zgl_tiles[tile->id % ZGL_TILES];
    if (slot->id == tile->id && slot->rev == tile->rev &&
        slot->w == tile->w && slot->h == tile->h) {
        return slot;   /* 核心系统底层抽象与内存语义契约 */
    }

    int shelf = (tile->bpp == 4) ? 1 : 0;
    if (g_zgl_shelf[shelf].x + tile->w > ZGL_ATLAS_DIM) {
        g_zgl_shelf[shelf].x = 0;
        g_zgl_shelf[shelf].y += g_zgl_shelf[shelf].row_h;
        g_zgl_shelf[shelf].row_h = 0;
    }
    if (g_zgl_shelf[shelf].y + tile->h > ZGL_ATLAS_DIM) {
        /* 内部辅助逻辑 */
        zgl_flush();
        zgl_atlas_reset();
    }
    slot->id = tile->id;
    slot->rev = tile->rev;
    slot->x = g_zgl_shelf[shelf].x;
    slot->y = g_zgl_shelf[shelf].y;
    slot->w = tile->w;
    slot->h = tile->h;
    slot->bpp = tile->bpp;
    g_zgl_shelf[shelf].x += tile->w;
    if (tile->h > g_zgl_shelf[shelf].row_h) g_zgl_shelf[shelf].row_h = tile->h;

    /* 模块核心语义抽象与接口调用契约 */
    zgl_flush();
    gl.BindTexture(ZGL_TEXTURE_2D, shelf ? g_zgl.atlas4 : g_zgl.atlas1);
    gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, slot->x, slot->y, slot->w, slot->h,
                     shelf ? ZGL_BGRA : ZGL_RED, ZGL_UNSIGNED_BYTE, tile->cov);
    return slot;
}

static void gl_glyph_run(zan_surface_t *s, const zan_glyph_run *run) {
    if (!run || run->count <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_TEXT)) { cpu_glyph_run(s, run); return; }
    u32 color = run->color;
    if (((color >> 24) & 0xFF) == 0) color |= 0xFF000000u;  /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        zgl_tile_slot *slot = zgl_tile_upload(tile);
        if (!slot) continue;
        /* 内部辅助逻辑 */
        if (!zgl_begin(s, ZGL_MODE_TEXT)) return;
        zgl_quad q;
        memset(&q, 0, sizeof(q));
        q.kind = (tile->flags & ZAN_TILE_RGBA) ? ZGL_K_TEXTRGBA
               : (tile->bpp == 4) ? ZGL_K_TEXT4 : ZGL_K_TEXT1;
        float x0 = (float)run->items[i].x, y0 = (float)run->items[i].y;
        float x1 = x0 + (float)tile->w, y1 = y0 + (float)tile->h;
        q.cx = (x0 + x1) / 2.0f;
        q.cy = (y0 + y1) / 2.0f;
        q.hw = (x1 - x0) / 2.0f;
        q.hh = (y1 - y0) / 2.0f;
        q.u0 = (float)slot->x / (float)ZGL_ATLAS_DIM;
        q.v0 = (float)slot->y / (float)ZGL_ATLAS_DIM;
        q.u1 = (float)(slot->x + slot->w) / (float)ZGL_ATLAS_DIM;
        q.v1 = (float)(slot->y + slot->h) / (float)ZGL_ATLAS_DIM;
        zgl_color(q.col0, color, 0);
        zgl_push(s, &q, x0, y0, x1, y1);
    }
}

/* 核心系统底层抽象与内存语义契约 */

static void gl_set_clip(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    /* 内部辅助逻辑 */
    (void)x0; (void)y0; (void)x1; (void)y1;
    if (g_zgl.target == s) zgl_flush();
}

static void gl_flush(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    if (g_zgl.target == s || g_zgl.count) zgl_flush();
    gl.Flush();
}

static void gl_read_pixels(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (t) zgl_readback(s, t);
}

/* 内部辅助逻辑 */
static int gl_present(zan_surface_t *s, void *native_window) {
    if (g_gl_state <= 0 || !native_window) return 0;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    /* 内部辅助逻辑 */
    zgl_upload(s, t);
    if (!zan_gl_ctx_present_begin(native_window, t->w, t->h)) return 0;

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->fbo);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, 0);
    /* 内部辅助逻辑 */
    gl.BlitFramebuffer(0, 0, t->w, t->h,
                       0, 0, t->w, t->h,
                       ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);
    zan_gl_ctx_present_end(native_window);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    return 1;
}

static void gl_drop_window(void *native_window) {
    if (g_gl_state <= 0 || !native_window) return;
    zan_gl_ctx_present_drop(native_window);
}

/* 内部辅助逻辑 */
static void gl_drop_surface(zan_surface_t *s) {
    if (s->id < 0 || s->id >= (int)(sizeof(g_zgl_targets) / sizeof(g_zgl_targets[0])))
        return;
    zgl_target *t = &g_zgl_targets[s->id];
    if (g_gl_state > 0 && t->fbo) {
        if (g_zgl.target == s) zgl_flush();
        if (!zan_gl_ctx_make_current()) return;
        zgl_cleanup_extensions(s->id);
        gl.DeleteFramebuffers(1, &t->fbo);
        gl.DeleteTextures(1, &t->tex);
        if (t->cov_fbo) gl.DeleteFramebuffers(1, &t->cov_fbo);
        if (t->cov_tex) gl.DeleteTextures(1, &t->cov_tex);
    }
    if (g_zgl.target == s) g_zgl.target = NULL;
    free(t->shadow);
    memset(t, 0, sizeof(*t));
}

static void gl_drop_tex(unsigned int tex) {
    if (g_gl_state > 0 && tex) {
        if (g_zgl.bitmap_tex == tex) {
            zgl_flush();
            g_zgl.bitmap_tex = 0;
        }
        zan_gl_ctx_make_current();
        gl.DeleteTextures(1, &tex);
    }
}

/* 底层系统交互与数据协议契约 */
#define ZGL_BITMAP_CAP 256
#define ZGL_BITMAP_BYTES (64u * 1024u * 1024u)
typedef struct {
    uint64_t serial, last_use;
    zgl_uint tex;
    size_t bytes;
} zgl_bitmap_texture;
static zgl_bitmap_texture g_bitmap_textures[ZGL_BITMAP_CAP];
static uint64_t g_bitmap_tick;
static size_t g_bitmap_bytes;

static void zgl_bitmap_drop(int slot) {
    zgl_bitmap_texture *t = &g_bitmap_textures[slot];
    if (t->tex) gl_drop_tex(t->tex);
    g_bitmap_bytes -= t->bytes;
    memset(t, 0, sizeof(*t));
}

static zgl_uint zgl_bitmap_tex(const zan_bitmap *img) {
    if (!zan_bitmap_valid(img) || g_gl_state <= 0) return 0;
    int slot = -1, oldest = 0;
    for (int i = 0; i < ZGL_BITMAP_CAP; i++) {
        zgl_bitmap_texture *t = &g_bitmap_textures[i];
        if (img->serial && t->tex && t->serial == img->serial) {
            t->last_use = ++g_bitmap_tick;
            return t->tex;
        }
        if (!t->tex && slot < 0) slot = i;
        if (t->tex && (!g_bitmap_textures[oldest].tex ||
                       t->last_use < g_bitmap_textures[oldest].last_use)) oldest = i;
    }
    if (slot < 0) { slot = oldest; zgl_bitmap_drop(slot); }
    size_t bytes = (size_t)img->width * (size_t)img->height * 4;
    while (g_bitmap_bytes && g_bitmap_bytes + bytes > ZGL_BITMAP_BYTES) {
        int victim = -1;
        for (int i = 0; i < ZGL_BITMAP_CAP; i++)
            if (g_bitmap_textures[i].tex && (victim < 0 ||
                g_bitmap_textures[i].last_use < g_bitmap_textures[victim].last_use))
                victim = i;
        if (victim < 0) break;
        zgl_bitmap_drop(victim);
    }
    zgl_flush();
    if (!zan_gl_ctx_make_current()) return 0;
    zgl_uint tex = 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(ZGL_TEXTURE_2D, tex);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, img->stride);
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, img->width, img->height, 0,
                  ZGL_BGRA, ZGL_UNSIGNED_BYTE, img->pixels);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
    if (gl.GetError() != ZGL_NO_ERROR) { gl.DeleteTextures(1, &tex); return 0; }
    g_bitmap_textures[slot].serial = img->serial;
    g_bitmap_textures[slot].last_use = ++g_bitmap_tick;
    g_bitmap_textures[slot].tex = tex;
    g_bitmap_textures[slot].bytes = bytes;
    g_bitmap_bytes += bytes;
    return tex;
}

static void zgl_blit_pixels_batch(zan_surface_t *s, const zan_bitmap *img,
                                  const float *quads, int count) {
    zgl_uint tex = zgl_bitmap_tex(img);
    if (!tex || !zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_blit_pixels_batch(s, img, quads, count);
        return;
    }
    if (g_zgl.bitmap_tex != tex) {
        zgl_flush();
        g_zgl.bitmap_tex = tex;
    }
    int iw = img->width, ih = img->height, i;
    for (i = 0; i < count; i++) {
        const float *q = quads + i * 10;
        u32 tint_raw;
        zgl_quad sq;
        float dx = q[0], dy = q[1], dw = q[2], dh = q[3];
        float sx = q[4], sy = q[5], sw = q[6], sh = q[7];
        memcpy(&tint_raw, q + 8, sizeof(u32));
        memset(&sq, 0, sizeof(sq));
        sq.kind = ZGL_K_BITMAP;
        if (dw <= 0.0f) dw = (float)iw;
        if (dh <= 0.0f) dh = (float)ih;
        if (sw <= 0.0f) { sx = 0.0f; sw = (float)iw; }
        if (sh <= 0.0f) { sy = 0.0f; sh = (float)ih; }
        sq.cx = dx + dw * 0.5f;
        sq.cy = dy + dh * 0.5f;
        sq.hw = dw * 0.5f;
        sq.hh = dh * 0.5f;
        sq.u0 = sx / (float)iw;
        sq.v0 = sy / (float)ih;
        sq.u1 = (sx + sw) / (float)iw;
        sq.v1 = (sy + sh) / (float)ih;
        zgl_color(sq.col0, tint_raw, 0);
        zgl_push(s, &sq, dx, dy, dx + dw, dy + dh);
    }
}

static void zgl_blit_pixels(zan_surface_t *s, const zan_bitmap *img,
                            int dx, int dy, int dw, int dh,
                            int sx, int sy, int sw, int sh) {
    float quad[10] = { (float)dx, (float)dy, (float)dw, (float)dh,
                       (float)sx, (float)sy, (float)sw, (float)sh, 0, 0 };
    u32 tint = 0xFFFFFFFFu;
    memcpy(&quad[8], &tint, sizeof(tint));
    zgl_blit_pixels_batch(s, img, quad, 1);
}

static const zan_gui_backend zan_gl_backend = {
    .name         = "gl",
    .clear_rect   = gl_clear_rect,
    .fill_rect    = gl_fill_rect,
    .fill_round   = gl_fill_round,
    .draw_round   = gl_draw_round,
    .surface_round = gl_surface_round,
    .fill_vgrad   = gl_fill_vgrad,
    .fill_grad    = gl_fill_grad,
    /* 内部辅助逻辑 */
    .shadow_round = NULL,
    .fill_circle  = gl_fill_circle,
    .draw_circle  = gl_draw_circle,
    .fill_radial  = gl_fill_radial,
    .fill_sector  = gl_fill_sector,
    .draw_line    = gl_draw_line,
    .polyline     = gl_polyline,
    .polybatch    = gl_polybatch,
    .blur         = NULL,
    .snapshot     = NULL,
    .restore      = NULL,
    .draw_text    = NULL,
    .glyph_run    = gl_glyph_run,
    .blit_pixels  = zgl_blit_pixels,
    .blit_pixels_batch = zgl_blit_pixels_batch,
    .set_clip     = gl_set_clip,
    .flush        = gl_flush,
    .read_pixels  = gl_read_pixels,
    .present      = gl_present,
    .drop_window  = gl_drop_window,
    .drop_surface = gl_drop_surface,
    .sync_to_cpu  = gl_sync_to_cpu,
    .sync_from_cpu = gl_sync_from_cpu,
};

/* 内部辅助逻辑 */
int zan_gui_internal_gl_install(void) {
    if (!zgl_init()) return 0;
    zan_gui_internal_set_backend(&zan_gl_backend);
    return 1;
}

/* 内部辅助逻辑 */
void zan_gui_internal_gl_drop_present(void) {
    if (g_gl_state <= 0) return;
    if (!zan_gl_ctx_make_current()) return;
    zgl_flush();
    zgl_cleanup_extensions(-1);
    ++g_gpu_epoch;
    for (int i = 0; i < ZGL_BITMAP_CAP; i++) zgl_bitmap_drop(i);
    for (int i = 0; i < g_surface_count; i++)
        if (g_surfaces[i]) gl_drop_surface(g_surfaces[i]);
    zan_gl_ctx_present_drop_all();
}

EXPORT i32 zan_gui_gpu_begin(i32 surface_id, zan_gpu_frame *frame) {
    if (!frame || surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || s->be != &zan_gl_backend || g_gl_state <= 0
        || !zan_gl_ctx_make_current()) return 0;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    zgl_upload(s, t);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    gl.Viewport(0, 0, s->width, s->height);
    frame->api = &gl;
    frame->framebuffer = t->fbo;
    frame->width = s->width;
    frame->height = s->height;
    frame->clip_x = s->clip_x0;
    frame->clip_y = s->clip_y0;
    frame->clip_w = s->clip_x1 - s->clip_x0;
    frame->clip_h = s->clip_y1 - s->clip_y0;
    frame->epoch = g_gpu_epoch;
    return 1;
}

EXPORT void zan_gui_gpu_end(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || s->be != &zan_gl_backend || g_gl_state <= 0) return;
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    gl.Disable(ZGL_DEPTH_TEST);
    gl.Disable(ZGL_SCISSOR_TEST);
    gl.Enable(ZGL_BLEND);
    gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                          ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
    gl.BindVertexArray(0);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    t->gpu_ahead = 1;
    s->painted = 1;
}

EXPORT u32 zan_gui_gpu_program(const char *vertex, const char *fragment) {
    if (!vertex || !fragment || g_gl_state <= 0
        || !zan_gl_ctx_make_current()) return 0;
    return zgl_link(vertex, fragment);
}

EXPORT u32 zan_gui_gpu_texture(const zan_bitmap *bitmap) {
    return zgl_bitmap_tex(bitmap);
}
