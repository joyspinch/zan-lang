/* gui_gl_backend.c -- the GPU rasterizer behind the backend seam.
 *
 * Part of the gui_runtime translation unit (#include'd by gui_runtime.c), like
 * the platform shells: it needs zan_surface_t, the glyph atlas and the CPU
 * backend as its own fallback.
 *
 * Shape of the thing:
 *
 *  - every primitive is one quad whose fragment shader evaluates a signed
 *    distance field, so corner rounding, rings, sector edges and thick lines
 *    come out anti-aliased without a coverage pass and without geometry;
 *  - quads accumulate into one vertex buffer and are submitted in as few draw
 *    calls as the state allows -- clip rectangle and colour travel *with* the
 *    vertex, so a whole frame of unrelated widgets is one call;
 *  - text arrives as the coverage tiles of gui_runtime_glyph.c, which are
 *    uploaded into an atlas texture once per (tile, revision) and then drawn
 *    from the GPU for every later frame that shows the same string;
 *  - GDI's per-channel (ClearType) coverage is preserved exactly, by blending
 *    with dual-source output (core since GL 3.3) rather than collapsing it to
 *    one alpha and losing subpixel AA.
 *
 * Primitives this backend does not implement yet (blur, shadow, snapshot /
 * restore, image blit) are left NULL in the vtable, so the seam runs the CPU
 * code for them; the frame is moved between GPU and CPU by the sync_* entries
 * around such a call. That is the point of a partial vtable: the GPU path can
 * land primitive by primitive, and correctness never depends on the part that
 * is not there yet.
 *
 * Presentation goes two ways: `present` blits the finished framebuffer into the
 * window's back buffer and swaps it (no CPU copy at all), and `read_pixels`
 * pulls the frame into the surface bitmap for the shells and windows that need
 * one -- the layered glass window, screenshots, pixel-compare tests. A shell
 * that has no window handle to offer, or a window GL cannot attach to, simply
 * keeps the read-back path. */

#include "gui_gl.h"
#include "gui_gl_context.c"

/* ---------------------------------------------------------------- plumbing */

static zan_gl_api gl;
static int g_gl_state = 0;   /* 0 untried, 1 ready, -1 unusable */

#define ZGL_VF 34            /* floats per vertex, see zgl_push */

typedef enum {
    ZGL_MODE_NONE = 0,
    ZGL_MODE_BLEND,     /* shapes, source-over */
    ZGL_MODE_REPLACE,   /* shapes, overwrite (clear_rect, opaque gradients) */
    ZGL_MODE_TEXT       /* glyph runs, per-channel coverage */
} zgl_mode;

/* Shape kinds, as the fragment shader switches on them. */
#define ZGL_K_RECT    0
#define ZGL_K_CIRCLE  1
#define ZGL_K_RADIAL  2
#define ZGL_K_SECTOR  3
#define ZGL_K_CAPSULE 4
#define ZGL_K_TEXT1   5   /* single-channel coverage tile (FreeType glyph) */
#define ZGL_K_TEXT4   6   /* per-channel coverage tile (GDI run) */

/* Tile side for the upload comparison below: 64x64 is 16 KiB of pixels, small
 * enough that a scrolled list or a hovered button touches few tiles, large
 * enough that a full-surface change is a few hundred TexSubImage2D calls. */
#define ZGL_TILE 64

typedef struct {
    zgl_uint tex, fbo;
    int w, h;
    int gpu_ahead;   /* GPU framebuffer holds pixels s->pixels does not */
    int cpu_ahead;   /* s->pixels holds pixels the GPU framebuffer does not */
    /* Last pixels this target uploaded or read back, so an upload can send the
     * tiles the CPU actually changed instead of the whole surface. NULL when
     * the allocation failed, which just means whole-surface uploads. */
    unsigned char *shadow;
} zgl_target;

static zgl_target g_zgl_targets[64];

/* One vertex buffer for every surface: draws are always flushed before the
 * render target changes, so the batch never spans two framebuffers. */
static struct {
    zgl_uint prog_shape, prog_text, vao, vbo;
    zgl_int  u_viewport_shape, u_viewport_text, u_atlas1, u_atlas4;
    zgl_uint atlas1, atlas4;         /* R8 and RGBA8 glyph atlases */
    float   *verts;
    size_t   count, cap;             /* in floats */
    zgl_mode mode;
    zan_surface_t *target;
} g_zgl;

/* --------------------------------------------------------------- shaders */

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
"in vec4 a_seg;\n"     /* capsule endpoints */
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
/* Surface coordinates are top-left origin, GL's are bottom-left: flip here so
 * everything above (and every clip rectangle) can stay in surface space. */
"    vec2 ndc = vec2(a_pos.x / uViewport.x * 2.0 - 1.0,\n"
"                    1.0 - a_pos.y / uViewport.y * 2.0);\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"}\n";

/* Shared prologue of both fragment shaders: the pixel this fragment covers, in
 * surface coordinates, and the clip test. */
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
"out vec4 o_color;\n"
/* Rounded-box distance with per-corner radius: a corner whose mask bit is
 * clear is square, which is how welded controls share a straight seam. */
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
/* grad_sample's three-stop lerp, in floats: `via` is used only when its flag
 * (v_kind.z) says the caller passed one. */
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
/* The round shapes measure distance from the pixel's top-left sample, which is
 * where cpu_fill_circle / cpu_draw_circle / cpu_fill_sector / cpu_fill_radial
 * take theirs (they walk integer dx, dy from the centre); everything else
 * measures from the pixel centre. Half a pixel of disagreement here is a
 * visible ring one pixel off its CPU position. */
"    vec2 lp = (kind >= 1 && kind <= 3) ? (p - 0.5) - v_center\n"
"                                       : p - v_center;\n"
"    float cov = 0.0;\n"
"    vec4 col = v_col0;\n"
"    if (kind == 0) {\n"
"        float sd = sd_round(lp, v_shape.xy, v_shape.z, int(v_kind.y + 0.5));\n"
"        if (v_shape.w > 0.0) sd = abs(sd + v_shape.w * 0.5) - v_shape.w * 0.5;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
/* Gradient direction, matching zan_gui_fill_grad_mask: 0 vertical, 1
 * horizontal, 2 to bottom-right, 3 to bottom-left. */
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
/* Filled: coverage ramps across the last half pixel of the radius. Stroked:
 * the ring straddles the radius, half its thickness on each side. */
"        float sd = (v_shape.w > 0.0)\n"
"                 ? abs(length(lp) - v_shape.z) - v_shape.w * 0.5\n"
"                 : length(lp) - v_shape.z;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
/* Soft glow: the CPU path's squared falloff, alpha only. */
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
/* Degrees clockwise from 12 o'clock, like zan_gui_fill_sector. */
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
"    }\n"
"    if (cov <= 0.0) discard;\n"
"    o_color = vec4(col.rgb, col.a * cov);\n"
"}\n";

/* Text: two fragment outputs, colour and per-channel coverage, blended as
 * SRC1_COLOR / ONE_MINUS_SRC1_COLOR. That is what keeps GDI's subpixel
 * (ClearType) coverage intact on the GPU -- averaging the three channels into
 * one alpha would visibly change every label in the app. */
static const char *ZGL_FS_TEXT =
"#version 330 core\n"
ZGL_FS_COMMON
"uniform sampler2D uAtlas1;\n"
"uniform sampler2D uAtlas4;\n"
/* The two dual-source slots are pinned here rather than left to the linker, so
 * which output the blender reads as SRC1 does not depend on the driver. */
"layout(location = 0, index = 0) out vec4 o_color;\n"
"layout(location = 0, index = 1) out vec4 o_cov;\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
"    vec3 cov = (kind == 5) ? vec3(texture(uAtlas1, v_uv).r)\n"
"                           : texture(uAtlas4, v_uv).rgb;\n"
"    if (cov.r + cov.g + cov.b <= 0.0) discard;\n"
"    o_color = vec4(v_col0.rgb, 1.0);\n"
"    vec3 c = cov * v_col0.a;\n"
"    o_cov = vec4(c, max(max(c.r, c.g), c.b));\n"
"}\n";

/* ------------------------------------------------------------ GL bootstrap */

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

/* The vertex layout, declared once and applied to both programs: the attribute
 * names are the same in each, so one VAO can feed them both. */
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
    /* Creating leaves the context current; binding it again is how the thread
     * that will draw claims it (and the last check that it is usable). */
    if (!zan_gl_ctx_make_current()) { zan_gl_ctx_destroy(); return 0; }
    if (!zan_gl_api_load(&gl, zan_gl_ctx_getproc)) { zan_gl_ctx_destroy(); return 0; }

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

/* ------------------------------------------------------- render targets */

/* The framebuffer a surface is drawn into, created on first use. Returns NULL
 * when the GPU cannot host this surface, and the caller falls back to the CPU
 * path for that primitive. */
static zgl_target *zgl_target_of(zan_surface_t *s) {
    if (s->id < 0 || s->id >= (int)(sizeof(g_zgl_targets) / sizeof(g_zgl_targets[0])))
        return NULL;
    zgl_target *t = &g_zgl_targets[s->id];
    if (t->fbo && (t->w != s->width || t->h != s->height)) {
        gl.DeleteFramebuffers(1, &t->fbo);
        gl.DeleteTextures(1, &t->tex);
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
        /* A fresh target starts from whatever the surface holds, so a backend
         * installed mid-run does not begin with a black window. */
        t->cpu_ahead = 1;
    }
    return t;
}

static void zgl_flush(void);

/* Copy one tile's rows between the surface and the shadow. */
static void zgl_shadow_store(zan_surface_t *s, zgl_target *t,
                             int x, int y, int tw, int th) {
    size_t row = (size_t)tw * 4;
    for (int r = 0; r < th; r++)
        memcpy(t->shadow + ((size_t)(y + r) * (size_t)s->width + (size_t)x) * 4,
               s->pixels + (size_t)(y + r) * (size_t)s->stride + (size_t)x,
               row);
}

/* Does this tile differ from what the GPU already has? */
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

/* s->pixels -> GPU, when the CPU path drew the most recent pixels.
 *
 * A frame that mixes CPU and GPU primitives (a blur, an image, a shadow) syncs
 * this way every time, so uploading the whole surface would spend the GPU's
 * bandwidth on pixels it already has. Comparing 64x64 tiles against the last
 * uploaded copy costs a memcmp over the surface -- cheap next to the transfer
 * -- and sends only what changed. The result is identical either way: the
 * pixels the GPU ends up with are exactly `s->pixels`. */
static void zgl_upload(zan_surface_t *s, zgl_target *t) {
    if (!t->cpu_ahead) return;
    gl.BindTexture(ZGL_TEXTURE_2D, t->tex);
    /* stride == width for every surface the runtime allocates; a padded one
     * would need a row loop, so assert the assumption rather than corrupt it. */
    if (!t->shadow) {
        if (s->stride == s->width) {
            gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, 0, 0, s->width, s->height,
                             ZGL_BGRA, ZGL_UNSIGNED_BYTE, s->pixels);
        } else {
            for (int y = 0; y < s->height; y++) {
                gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, 0, y, s->width, 1,
                                 ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                                 s->pixels + (size_t)y * (size_t)s->stride);
            }
        }
        t->cpu_ahead = 0;
        return;
    }

    /* Sub-rectangles come out of a wider image, so GL has to be told the real
     * row length; it goes back to 0 ("as wide as the transfer") afterwards. */
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, s->stride);
    for (int y = 0; y < s->height; y += ZGL_TILE) {
        int th = s->height - y < ZGL_TILE ? s->height - y : ZGL_TILE;
        for (int x = 0; x < s->width; x += ZGL_TILE) {
            int tw = s->width - x < ZGL_TILE ? s->width - x : ZGL_TILE;
            if (!zgl_tile_dirty(s, t, x, y, tw, th)) continue;
            gl.TexSubImage2D(ZGL_TEXTURE_2D, 0, x, y, tw, th,
                             ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                             s->pixels + (size_t)y * (size_t)s->stride + (size_t)x);
            zgl_shadow_store(s, t, x, y, tw, th);
        }
    }
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
    t->cpu_ahead = 0;
}

/* Staging rows for GPU readback: GL hands the frame back bottom-up and the
 * surface is top-down, so the flip runs through here. Reused across calls --
 * present-path shells read back every frame, and a malloc/free of
 * W*H*4 per present showed up as pure allocator churn. Grow-only, like the
 * polyline coverage scratch on the CPU side. */
static unsigned char *g_zgl_rb = NULL;
static size_t g_zgl_rb_cap = 0;

/* GPU -> s->pixels, for the shells' present path and for any primitive still
 * running on the CPU. */
static void zgl_readback(zan_surface_t *s, zgl_target *t) {
    if (!t->gpu_ahead) return;
    if (s->width <= 0 || s->height <= 0) return;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    /* GL hands back rows bottom-up; the surface is top-down. */
    size_t row = (size_t)s->width * 4;
    size_t need = row * (size_t)s->height;
    if (need > g_zgl_rb_cap) {
        unsigned char *ng = (unsigned char *)realloc(g_zgl_rb, need);
        if (!ng) return;   /* keep the old block; skip this readback */
        g_zgl_rb = ng;
        g_zgl_rb_cap = need;
    }
    gl.ReadPixels(0, 0, s->width, s->height, ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                  g_zgl_rb);
    for (int y = 0; y < s->height; y++) {
        memcpy(s->pixels + (size_t)y * (size_t)s->stride,
               g_zgl_rb + row * (size_t)(s->height - 1 - y), row);
    }
    /* The surface and the GPU now agree, so this is also the newest shadow --
     * without it every tile would read as dirty on the next upload. */
    if (t->shadow) zgl_shadow_store(s, t, 0, 0, s->width, s->height);
    t->gpu_ahead = 0;
}

static void gl_sync_to_cpu(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    zgl_readback(s, t);
    t->cpu_ahead = 1;   /* whatever runs next writes into s->pixels */
}

static void gl_sync_from_cpu(zan_surface_t *s) {
    if (g_gl_state <= 0) return;
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    if (g_zgl.target && g_zgl.target != s) zgl_flush();
    zgl_upload(s, t);
}

/* --------------------------------------------------------------- batching */

static void zgl_flush(void) {
    if (!g_zgl.count || !g_zgl.target) { g_zgl.count = 0; return; }
    zan_surface_t *s = g_zgl.target;
    zgl_target *t = zgl_target_of(s);
    if (!t) { g_zgl.count = 0; return; }

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    gl.Viewport(0, 0, t->w, t->h);
    gl.BindVertexArray(g_zgl.vao);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo);
    gl.BufferData(ZGL_ARRAY_BUFFER,
                  (zgl_sizeiptr)(g_zgl.count * sizeof(float)),
                  g_zgl.verts, ZGL_STREAM_DRAW);

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
        /* Colour blends against the per-channel coverage; the alpha channel takes
         * that coverage as one scalar, so text on a transparent surface
         * accumulates alpha the way blend_over does. */
        gl.BlendFuncSeparate(ZGL_SRC1_COLOR, ZGL_ONE_MINUS_SRC1_COLOR,
                             ZGL_SRC1_ALPHA, ZGL_ONE_MINUS_SRC1_ALPHA);
    } else {
        gl.UseProgram(g_zgl.prog_shape);
        zgl_bind_attribs(g_zgl.prog_shape);
        gl.Uniform2f(g_zgl.u_viewport_shape, (float)t->w, (float)t->h);
        if (g_zgl.mode == ZGL_MODE_REPLACE) {
            gl.Disable(ZGL_BLEND);
        } else {
            gl.Enable(ZGL_BLEND);
            /* Straight-alpha source-over, as blend_over does it: the colour
             * weights by the source alpha, the alpha channel accumulates
             * (sa + da*(1-sa)) so a surface cleared transparent keeps a
             * meaningful alpha for the layered-window present. */
            gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                 ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        }
    }

    gl.DrawArrays(ZGL_TRIANGLES, 0, (zgl_sizei)(g_zgl.count / ZGL_VF));
    g_zgl.count = 0;
    t->gpu_ahead = 1;
}

/* Start (or continue) a batch for this surface in this mode; returns 0 when the
 * GPU cannot take the primitive. */
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
    float cx, cy;              /* shape centre, surface pixels */
    float hw, hh;              /* half extents */
    float radius, stroke;
    int   kind;
    float p0, p1, p2;          /* kind-specific */
    float col0[4], col1[4], col2[4];
    float seg[4];
    float u0, v0, u1, v1;      /* atlas coords for textured kinds */
} zgl_quad;

static void zgl_color(float *out, u32 c, int force_opaque) {
    out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(c & 0xFF) / 255.0f;
    u32 a = (c >> 24) & 0xFF;
    out[3] = force_opaque ? 1.0f : (float)a / 255.0f;
}

/* Emits the quad covering [x0,x1) x [y0,y1) with the fragment parameters of
 * `q`, clipped to the surface's clip window. */
static void zgl_push(zan_surface_t *s, const zgl_quad *q,
                     float x0, float y0, float x1, float y1) {
    /* Trim to the clip window up front: the fragment shader still tests, but a
     * quad entirely outside costs nothing this way. */
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
    /* uv follows the quad's corners so a trimmed quad still samples the right
     * part of the tile. */
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
        /* Textured kinds: map the quad's own extent onto the tile. */
        float fu = (q->hw > 0.0f) ? (px - (q->cx - q->hw)) / (2.0f * q->hw) : 0.0f;
        float fv = (q->hh > 0.0f) ? (py - (q->cy - q->hh)) / (2.0f * q->hh) : 0.0f;
        v[32] = q->u0 + du * fu;
        v[33] = q->v0 + dv * fv;
        g_zgl.count += ZGL_VF;
    }
}

/* ------------------------------------------------------------- primitives */

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

static void gl_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 top, u32 bottom) {
    /* Overwrites its rect (alpha forced opaque), like the CPU path -- except on
     * the rounded corners, where the arc has to blend or the AA is lost. */
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
    /* A stroke straddles the radius, so the quad has to reach past it. */
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
    gl_capsule(s, (float)x0 + 0.5f, (float)y0 + 0.5f,
               (float)x1 + 0.5f, (float)y1 + 0.5f, half, c);
}

static void gl_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                        int thickness, int fx) {
    if (n < 2) return;
    /* The CPU path rasterizes the whole path in one coverage pass so a pixel is
     * blended once; capsules per segment double-blend where they overlap, which
     * shows on a translucent stroke. Keep those on the CPU. */
    if (((c >> 24) & 0xFF) != 255 || !zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_polyline(s, pts, n, c, thickness, fx);
        return;
    }
    float scale = (fx > 0) ? 1.0f / (float)(1 << fx) : 1.0f;
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale + 0.5f;
        float ay = (float)pts[i * 2 + 1] * scale + 0.5f;
        float bx = (float)pts[(i + 1) * 2] * scale + 0.5f;
        float by = (float)pts[(i + 1) * 2 + 1] * scale + 0.5f;
        gl_capsule(s, ax, ay, bx, by, half, c);
    }
}

/* ------------------------------------------------------------------- text */

/* Where a coverage tile lives in the GPU atlas. Tile ids are never reused
 * (gui_runtime_glyph.c hands out fresh ones), so a stale entry is only wasted
 * space and the whole atlas is reset when it fills up. */
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
        return slot;   /* already on the GPU: nothing to upload */
    }

    int shelf = (tile->bpp == 4) ? 1 : 0;
    if (g_zgl_shelf[shelf].x + tile->w > ZGL_ATLAS_DIM) {
        g_zgl_shelf[shelf].x = 0;
        g_zgl_shelf[shelf].y += g_zgl_shelf[shelf].row_h;
        g_zgl_shelf[shelf].row_h = 0;
    }
    if (g_zgl_shelf[shelf].y + tile->h > ZGL_ATLAS_DIM) {
        /* Full: drop everything rather than evict cleverly -- it happens when
         * the app changed its whole font set, and one re-upload of what the
         * next frames draw costs less than tracking ages. */
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

    /* A pending batch may sample the region about to be overwritten. */
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
    if (((color >> 24) & 0xFF) == 0) color |= 0xFF000000u;  /* as text always did */
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        zgl_tile_slot *slot = zgl_tile_upload(tile);
        if (!slot) continue;
        /* zgl_tile_upload may have flushed (atlas reset / region overwrite),
         * which leaves the batch empty but the mode intact. */
        if (!zgl_begin(s, ZGL_MODE_TEXT)) return;
        zgl_quad q;
        memset(&q, 0, sizeof(q));
        q.kind = (tile->bpp == 4) ? ZGL_K_TEXT4 : ZGL_K_TEXT1;
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

/* --------------------------------------------------------- frame boundaries */

static void gl_set_clip(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    /* The clip travels with each vertex, so there is no GL state to change --
     * but a batch already built carries the old window. */
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

/* Straight to the screen: the finished FBO is blitted into the back buffer of
 * the window's GL child and swapped, so the frame never travels through
 * s->pixels. Anything this cannot do (no GL child window for that handle, a
 * surface whose newest pixels are on the CPU side) returns 0 and the shell
 * blits the bitmap exactly as it did before. */
static int gl_present(zan_surface_t *s, void *native_window) {
    if (g_gl_state <= 0 || !native_window) return 0;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    /* The CPU drew the newest pixels (a blur, a shadow, an image): upload them
     * so the swap shows this frame and not the last GPU one. */
    zgl_upload(s, t);
    if (!zan_gl_ctx_present_begin(native_window, t->w, t->h)) return 0;

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->fbo);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, 0);
    /* No flip: the vertex shader already turns surface coordinates upside down
     * on the way into the framebuffer, so the FBO and the window agree on
     * GL's bottom-up rows -- flipping the frame here is what read_pixels does
     * for the top-down CPU bitmap, not what the screen wants. */
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

/* Surface ids are recycled, so the texture, framebuffer and upload shadow of a
 * destroyed surface must not be inherited by the next one that lands in the
 * slot -- it would start from stale pixels and see stale tiles as unchanged. */
static void gl_drop_surface(zan_surface_t *s) {
    if (s->id < 0 || s->id >= (int)(sizeof(g_zgl_targets) / sizeof(g_zgl_targets[0])))
        return;
    zgl_target *t = &g_zgl_targets[s->id];
    if (g_gl_state > 0 && t->fbo) {
        if (g_zgl.target == s) zgl_flush();
        zan_gl_ctx_make_current();
        gl.DeleteFramebuffers(1, &t->fbo);
        gl.DeleteTextures(1, &t->tex);
    }
    if (g_zgl.target == s) g_zgl.target = NULL;
    free(t->shadow);
    memset(t, 0, sizeof(*t));
}

static const zan_gui_backend zan_gl_backend = {
    .name         = "gl",
    .clear_rect   = gl_clear_rect,
    .fill_rect    = gl_fill_rect,
    .fill_round   = gl_fill_round,
    .draw_round   = gl_draw_round,
    .fill_vgrad   = gl_fill_vgrad,
    .fill_grad    = gl_fill_grad,
    /* shadow_round, blur, snapshot, restore and blit_image are the CPU's for
     * now: the seam syncs the frame across for them (sync_to_cpu below). */
    .shadow_round = NULL,
    .fill_circle  = gl_fill_circle,
    .draw_circle  = gl_draw_circle,
    .fill_radial  = gl_fill_radial,
    .fill_sector  = gl_fill_sector,
    .draw_line    = gl_draw_line,
    .polyline     = gl_polyline,
    .blur         = NULL,
    .snapshot     = NULL,
    .restore      = NULL,
    .draw_text    = NULL,
    .glyph_run    = gl_glyph_run,
    .blit_image   = NULL,
    .set_clip     = gl_set_clip,
    .flush        = gl_flush,
    .read_pixels  = gl_read_pixels,
    .present      = gl_present,
    .drop_window  = gl_drop_window,
    .drop_surface = gl_drop_surface,
    .sync_to_cpu  = gl_sync_to_cpu,
    .sync_from_cpu = gl_sync_from_cpu,
};

/* Brings up a context and installs the GPU backend, returning 0 (and leaving
 * every surface on the CPU) when this machine cannot provide GL 3.3 core. */
int zan_gui_internal_gl_install(void) {
    if (!zgl_init()) return 0;
    zan_gui_internal_set_backend(&zan_gl_backend);
    return 1;
}

/* Tears down every GL child window, for the moment the application switches
 * back to the CPU rasterizer: the shells then blit their bitmaps into a client
 * area nothing covers any more. Doing nothing before GL ever came up is the
 * whole point of the state check. */
void zan_gui_internal_gl_drop_present(void) {
    if (g_gl_state <= 0) return;
    zan_gl_ctx_present_drop_all();
}
