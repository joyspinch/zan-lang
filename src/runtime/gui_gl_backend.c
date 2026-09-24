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
/* GL 3.3 core; local to this backend's coverage pass. */
#define ZGL_MAX 0x8008
#define ZGL_FUNC_ADD 0x8006
static void (*zgl_blend_equation)(zgl_enum);
static int g_gl_state = 0;   /* 0 untried, 1 ready, -1 unusable */

#define ZGL_VF 34            /* floats per vertex, see zgl_push */

typedef enum {
    ZGL_MODE_NONE = 0,
    ZGL_MODE_BLEND,     /* shapes, source-over */
    ZGL_MODE_REPLACE,   /* shapes, overwrite (clear_rect, opaque gradients) */
    ZGL_MODE_TEXT,      /* glyph runs, per-channel coverage */
    ZGL_MODE_UNION      /* capsules into coverage FBO, maximum not source-over */
    /* ZGL_MODE_COMPOSITE was folded into ZGL_MODE_BLEND: the shape shader's
     * uComposite path does the straight-alpha source-over itself and is fed by
     * per-primitive state, not by a distinct pipeline mode. */
} zgl_mode;

/* Shape kinds, as the fragment shader switches on them. */
#define ZGL_K_RECT    0
#define ZGL_K_CIRCLE  1
#define ZGL_K_RADIAL  2
#define ZGL_K_SECTOR  3
#define ZGL_K_CAPSULE 4
#define ZGL_K_TEXT1   5   /* single-channel coverage tile (FreeType glyph) */
#define ZGL_K_TEXT4   6   /* per-channel coverage tile (GDI run) */
#define ZGL_K_UNION   7   /* sample completed polyline coverage */
#define ZGL_K_TEXTRGBA 9 /* color glyph tile: own BGRA + straight alpha */
#define ZGL_K_SURFACE 8   /* combined rounded fill and border */
#define ZGL_K_SPRITE  10  /* textured quad from the sprite registry (A356) */

/* Tile side for the upload comparison below: 64x64 is 16 KiB of pixels, small
 * enough that a scrolled list or a hovered button touches few tiles, large
 * enough that a full-surface change is a few hundred TexSubImage2D calls. */
#define ZGL_TILE 64

typedef struct {
    zgl_uint tex, fbo;
    zgl_uint cov_tex, cov_fbo; /* lazy, surface-sized R8 union scratch */
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
    zgl_int  u_viewport_shape, u_viewport_text, u_atlas1, u_atlas4, u_coverage,
             u_destination;
    zgl_uint atlas1, atlas4;         /* R8 and RGBA8 glyph atlases */
    zgl_int  u_atlas2;               /* sprite batch sampler (shape program) */
    int      sprite_handle;          /* handle whose texture unit 2 holds */
    zgl_uint sprite_tex;             /* 0 = no sprite batch in flight */
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
"    /* blend_over, byte for byte: the stored RGB is normalised by the RESULT\n"
"     * alpha (straight alpha) and quantised with integer division, so the\n"
"     * layered present and any CPU readback see the same bytes the CPU path\n"
"     * would have written for the same geometry. */\n"
"    return vec4(floor((s.rgb*s.a+d.rgb*da)/a), a)/255.0;\n"
"}\n"
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
/* Circle/sector and capsules use pixel centres. The radial glow alone keeps
 * the CPU's integer-lattice squared-distance falloff. */
"    vec2 lp = p - v_center;\n"
"    if (kind == 2) lp -= vec2(0.5);\n"
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
/* Textured sprite: straight-alpha texel scaled by the tint colour; the
   regular BLEND pipeline does the source-over. */
"    if (kind == 10) {\n"
"        col = texture(uAtlas2, v_uv) * v_col0;\n"
"        cov = 1.0;\n"
"    }\n"
"    if (cov <= 0.0) discard;\n"
"    o_color = (kind == 4 && v_kind.y > 0.5) ? vec4(cov)\n"
"                                          : vec4(col.rgb, col.a * cov);\n"
"    /* Straight-alpha source-over computed in the shader, byte-faithful to the\n"
"     * CPU's blend_over: fixed-function blending accumulates RGB without the\n"
"     * /alpha normalisation, which on transparent destinations (shaped/glass\n"
"     * windows) fringes every translucent edge. Kinds 7/8 always need it; the\n"
"     * coverage/destination samplers are bound only for those draws. */\n"
"    if (kind == 7 || kind == 8) o_color = over(o_color);\n"
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
"    if (kind == 9) {\n"
/* Color glyph tile (CBDT emoji): own straight-alpha BGRA pixels, blended
 * source-over through the same dual-source equation -- out = t.rgb * t.a
 * + dst * (1 - t.a); the run color does not participate. */
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

/* ------------------------------------------------------------- 3D pipeline
 *
 * Depth-tested, texture-mapped triangles drawn into the surface's FBO. The
 * 2D batch above owns state (blending, scissor-off, no depth), so every 3D
 * draw is a self-contained pass: flush the 2D batch first, attach the lazy
 * depth renderbuffer, set its own program/VAO, draw, and restore 2D state.
 * Y is flipped inside the shader (surface space is top-down, like the 2D
 * path), so an app composes one matrix chain and both backends agree. */

#define ZGL_MESH_VCAP 24
typedef struct {
    zgl_uint vbo, ebo, vao;
    int index_count;
    int used;
} zgl_mesh;

static zgl_mesh g_zgl_meshes[ZGL_MESH_VCAP];

/* Lazily created per-target depth attachment (kept across frames; grown with
 * the surface resize in zgl_target_of by the drop-recreate there). */
static zgl_uint zgl_depth_of(zgl_target *t) {
    static zgl_uint rb[64];
    static int rw[64], rh[64];
    if (t - g_zgl_targets < 0 || t - g_zgl_targets >= 64) return 0;
    int slot = (int)(t - g_zgl_targets);
    if (rb[slot] && rw[slot] == t->w && rh[slot] == t->h) return rb[slot];
    if (rb[slot]) gl.DeleteRenderbuffers(1, &rb[slot]);
    rb[slot] = 0;
    gl.GenRenderbuffers(1, &rb[slot]);
    gl.BindRenderbuffer(ZGL_RENDERBUFFER, rb[slot]);
    gl.RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH_COMPONENT16,
                           t->w, t->h);
    gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_DEPTH_ATTACHMENT,
                               ZGL_RENDERBUFFER, rb[slot]);
    rw[slot] = t->w;
    rh[slot] = t->h;
    return rb[slot];
}

/* Texture for a draw: the runtime's image cache ("file path" or "mem:" key),
 * uploaded linearly sampled; a 1x1 white stand-in when there is none, so a
 * plain-coloured mesh needs no null branch in the shader. */
static zgl_uint zgl_3d_texture(const char *path, int *out_w, int *out_h) {
    static zgl_uint white = 0;
    struct { const char *key; zgl_uint tex; int w, h; } cache[8];
    static int cache_n = 0;
    *out_w = *out_h = 1;
    if (!white) {
        gl.GenTextures(1, &white);
        gl.BindTexture(ZGL_TEXTURE_2D, white);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_LINEAR);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_LINEAR);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
        unsigned int px = 0xFFFFFFFFu;
        gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, 1, 1, 0, ZGL_RGBA,
                      ZGL_UNSIGNED_BYTE, &px);
    }
    if (!path || !path[0]) return white;
    zan_img_t *e = zan_img_find(path);
    if (!e && strncmp(path, "mem:", 4) == 0) e = zan_img_mem_find(path);
    if (!e) return white;
    for (int i = 0; i < cache_n; i++)
        if (strcmp(cache[i].key, path) == 0) {
            *out_w = cache[i].w; *out_h = cache[i].h;
            return cache[i].tex;
        }
    zgl_uint tex = 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(ZGL_TEXTURE_2D, tex);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
    /* zan_img_t holds ARGB32 (a<<24|r<<16|g<<8|b, memory B,G,R,A); GL wants
     * bytes in sampling order -- BGRA reads the same memory straight across. */
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, e->w, e->h, 0, ZGL_BGRA,
                  ZGL_UNSIGNED_BYTE, e->pix);
    if (gl.GetError() != ZGL_NO_ERROR) { gl.DeleteTextures(1, &tex); return white; }
    if (cache_n < 8) {
        cache[cache_n].key = strdup(path);
        cache[cache_n].tex = tex;
        cache[cache_n].w = e->w;
        cache[cache_n].h = e->h;
        cache_n++;
    } else {
        /* Cache full: keep the texture alive for this frame only. */
        return tex;
    }
    *out_w = e->w;
    *out_h = e->h;
    return tex;
}

static const char *ZGL_3D_VS =
"#version 330 core\n"
"uniform mat4 uMVP;\n"
"uniform vec2 uViewport;\n"
"in vec3 a_pos;\n"
"in vec3 a_normal;\n"
"in vec2 a_uv;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"void main() {\n"
"    v_uv = a_uv;\n"
"    v_normal = a_normal;\n"
"    vec4 clip = uMVP * vec4(a_pos, 1.0);\n"
/* Surface space is top-down: flip Y after projection so a Zan-composed matrix
 * (Math3D.zan, +y up, -z forward) lands the way the 2D path reads pixels. */
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

static const char *ZGL_3D_FS =
"#version 330 core\n"
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"out vec4 frag;\n"
"void main() {\n"
/* Half-Lambert wrap keeps back faces readable without a second light. */
"    float lam = clamp(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.6))) * 0.5 + 0.5, 0.0, 1.0);\n"
"    float shade = 0.55 + 0.45 * lam;\n"
"    vec4 tex = texture(uTex, v_uv);\n"
"    frag = vec4(tex.rgb * uColor.rgb * shade, tex.a * uColor.a);\n"
"}\n";

static zgl_uint g_zgl_prog3d;
static zgl_int g_zgl_u3d_mvp, g_zgl_u3d_tex, g_zgl_u3d_color;

static int gl_mesh_create(zan_surface_t *s, const zan_mesh_data *m) {
    (void)s;
    if (g_gl_state <= 0 || !m || !m->verts || m->count <= 0 ||
        m->count > 65536 || m->index_count <= 0 || !m->indices) return 0;
    int slot = -1;
    for (int i = 0; i < ZGL_MESH_VCAP; i++)
        if (!g_zgl_meshes[i].used) { slot = i; break; }
    if (slot < 0) return 0;
    zgl_mesh *ms = &g_zgl_meshes[slot];
    if (!g_zgl_prog3d) {
        g_zgl_prog3d = zgl_link(ZGL_3D_VS, ZGL_3D_FS);
        if (!g_zgl_prog3d) return 0;
        g_zgl_u3d_mvp = gl.GetUniformLocation(g_zgl_prog3d, "uMVP");
        g_zgl_u3d_tex = gl.GetUniformLocation(g_zgl_prog3d, "uTex");
        g_zgl_u3d_color = gl.GetUniformLocation(g_zgl_prog3d, "uColor");
    }
    zan_gl_ctx_make_current();
    gl.GenVertexArrays(1, &ms->vao);
    gl.BindVertexArray(ms->vao);
    gl.GenBuffers(1, &ms->vbo);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, ms->vbo);
    gl.BufferData(ZGL_ARRAY_BUFFER,
                  (zgl_sizeiptr)(size_t)m->count * 8 * sizeof(float),
                  m->verts, ZGL_STATIC_DRAW);
    gl.GenBuffers(1, &ms->ebo);
    gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, ms->ebo);
    gl.BufferData(ZGL_ELEMENT_ARRAY_BUFFER,
                  (zgl_sizeiptr)(size_t)m->index_count * sizeof(unsigned short),
                  m->indices, ZGL_STATIC_DRAW);
    struct { const char *name; int size; size_t off; } a[] = {
        { "a_pos", 3, 0 }, { "a_normal", 3, 3 * sizeof(float) },
        { "a_uv", 2, 6 * sizeof(float) },
    };
    for (size_t i = 0; i < 3; i++) {
        zgl_int loc = gl.GetAttribLocation(g_zgl_prog3d, a[i].name);
        if (loc >= 0) {
            gl.EnableVertexAttribArray((zgl_uint)loc);
            gl.VertexAttribPointer((zgl_uint)loc, a[i].size, ZGL_FLOAT,
                                   ZGL_FALSE, 8 * (zgl_sizei)sizeof(float),
                                   (const void *)a[i].off);
        }
    }
    gl.BindVertexArray(0);
    ms->index_count = m->index_count;
    ms->used = 1;
    return slot + 1;   /* 1-based mesh id; 0 stays "no mesh" */
}

static int gl_draw3d(zan_surface_t *s, int mesh, const zan_draw3d *d) {
    if (g_gl_state <= 0 || !d) return 0;
    if (mesh <= 0 || mesh > ZGL_MESH_VCAP || !g_zgl_meshes[mesh - 1].used)
        return 0;
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    zgl_flush();               /* 2D batch lands before the 3D pass */
    zgl_upload(s, t);          /* newest CPU pixels in, incl. clear_rect */
    if (!g_zgl_prog3d) return 0;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    zgl_depth_of(t);
    gl.Viewport(0, 0, t->w, t->h);
    /* The depth renderbuffer starts at 1.0 and only the 3D pass writes it, so
     * clearing it here is what makes two 3D passes on one frame independent;
     * colour keeps whatever the 2D passes painted. */
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_DEPTH_BUFFER_BIT);
    gl.Enable(ZGL_DEPTH_TEST);
    gl.DepthFunc(ZGL_LEQUAL);
    gl.Disable(ZGL_BLEND);
    gl.UseProgram(g_zgl_prog3d);
    gl.UniformMatrix4fv(g_zgl_u3d_mvp, 1, ZGL_FALSE, d->mvp);
    gl.Uniform4f(g_zgl_u3d_color,
                 (float)((d->color >> 16) & 0xFF) / 255.0f,
                 (float)((d->color >> 8) & 0xFF) / 255.0f,
                 (float)(d->color & 0xFF) / 255.0f,
                 (float)((d->color >> 24) & 0xFF) / 255.0f);
    int tw, th;
    zgl_uint tex = zgl_3d_texture(d->texture, &tw, &th);
    gl.ActiveTexture(ZGL_TEXTURE0);
    gl.BindTexture(ZGL_TEXTURE_2D, tex);
    gl.Uniform1i(g_zgl_u3d_tex, 0);
    zgl_mesh *ms = &g_zgl_meshes[mesh - 1];
    gl.BindVertexArray(ms->vao);
    gl.DrawElements(ZGL_TRIANGLES, ms->index_count, ZGL_UNSIGNED_SHORT, 0);
    gl.BindVertexArray(0);
    gl.Disable(ZGL_DEPTH_TEST);
    gl.Enable(ZGL_BLEND);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    t->gpu_ahead = 1;
    return 1;
}

static void gl_mesh_drop_all(void) {
    if (g_gl_state <= 0) return;
    for (int i = 0; i < ZGL_MESH_VCAP; i++) {
        zgl_mesh *ms = &g_zgl_meshes[i];
        if (!ms->used) continue;
        gl.DeleteBuffers(1, &ms->vbo);
        gl.DeleteBuffers(1, &ms->ebo);
        gl.DeleteVertexArrays(1, &ms->vao);
        memset(ms, 0, sizeof(*ms));
    }
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
    if (g_zgl.mode == ZGL_MODE_UNION)
        gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    zgl_blend_equation(g_zgl.mode == ZGL_MODE_UNION ? ZGL_MAX : ZGL_FUNC_ADD);
    gl.Viewport(0, 0, t->w, t->h);
    gl.BindVertexArray(g_zgl.vao);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo);
    gl.BufferData(ZGL_ARRAY_BUFFER,
                  (zgl_sizeiptr)(g_zgl.count * sizeof(float)),
                  g_zgl.verts, ZGL_STREAM_DRAW);
    /* Kinds 7/8 do their source-over in the shader and need the destination
     * texels as they sit on the GPU right now; a snapshot that lags the
     * framebuffer would composite against stale pixels. */
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
        /* Colour blends against the per-channel coverage; the alpha channel takes
         * that coverage as one scalar, so text on a transparent surface
         * accumulates alpha the way blend_over does. */
        gl.BlendFuncSeparate(ZGL_SRC1_COLOR, ZGL_ONE_MINUS_SRC1_COLOR,
                             ZGL_SRC1_ALPHA, ZGL_ONE_MINUS_SRC1_ALPHA);
    } else {
        gl.UseProgram(g_zgl.prog_shape);
        zgl_bind_attribs(g_zgl.prog_shape);
        gl.Uniform2f(g_zgl.u_viewport_shape, (float)t->w, (float)t->h);
        gl.ActiveTexture(ZGL_TEXTURE0);
        /* Union capsules write coverage into the scratch R8 attachment; nothing
         * is sampled. The shader-over kinds sample the completed coverage and
         * the destination snapshot below. */
        if (g_zgl.mode == ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, 0);
        } else if (shader_over) {
            /* Snapshot the destination at its current state, then draw over
             * that copy with blending disabled so the shader sees the true
             * destination and writes the composited result, not a blend of
             * itself. Snapshot is a process-wide scratch: sized on demand,
             * copied for each draw batch that needs it, freed with the context. */
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
                /* Snapshot unavailable: skip the shader-over path for this
                 * batch rather than composite against a missing texture. */
                shader_over = 0;
            }
        }
        /* Never sample the attachment being written (including union passes). */
        if (g_zgl.mode != ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, t->cov_tex);
            gl.Uniform1i(g_zgl.u_coverage, 0);
        }
        /* Sprite batch texture rides unit 2 (0 = coverage, 1 = destination
         * snapshot / text atlas4). Bound whenever a sprite batch is in
         * flight; sticky across flushes until the handle changes. */
        if (g_zgl.sprite_tex) {
            gl.ActiveTexture(ZGL_TEXTURE2);
            gl.BindTexture(ZGL_TEXTURE_2D, g_zgl.sprite_tex);
            gl.Uniform1i(g_zgl.u_atlas2, 2);
            gl.ActiveTexture(ZGL_TEXTURE0);
        }
        if (g_zgl.mode == ZGL_MODE_REPLACE || shader_over) {
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
    if (g_zgl.mode != ZGL_MODE_UNION) t->gpu_ahead = 1;
    zgl_blend_equation(ZGL_FUNC_ADD);
    /* shader-over batches ran with blending disabled; restore the default
     * enabled state so a later REPLACE batch (which relies on Disable) and the
     * normal blend path both start from a known state. */
    gl.Enable(ZGL_BLEND);
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
    zgl_flush(); /* shader-over kinds composite against a destination snapshot;
                    they must not sit in a batch blended a second time */
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
    /* Thin Wu lines retain the legacy pixel-index convention. */
    float offset = thickness > 1 ? 0.0f : 0.5f;
    gl_capsule(s, (float)x0 + offset, (float)y0 + offset,
               (float)x1 + offset, (float)y1 + offset, half, c);
}

/* GL_MAX is idempotent even at fractional AA edges. Accumulate into a
 * separate R8 attachment, then composite once; never blend capsules directly
 * into the colour target. Scratch is lazy and follows the target lifetime. */
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
    /* Bound clear and composite to this path intersected with the clip. Every
     * sampled texel is cleared; stale coverage elsewhere is never sampled. */
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
    zgl_flush(); /* painter order before clearing/reusing scratch */
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        /* Resource failure only, not a rasterization workaround. */
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
    gl.Disable(ZGL_SCISSOR_TEST); /* normal clipping travels in vertices */
    g_zgl.mode = ZGL_MODE_UNION;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale;
        float ay = (float)pts[i * 2 + 1] * scale;
        float bx = (float)pts[i * 2 + 2] * scale;
        float by = (float)pts[i * 2 + 3] * scale;
        float dx = bx - ax, dy = by - ay;
        if (dx * dx + dy * dy < 0.0001f) continue; /* CPU degenerate policy */
        gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
    }
    zgl_flush(); /* includes all segments across allocation-pressure flushes */
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush(); /* finish sampling before scratch is overwritten/released */
}

/* Per-path CPU fallback shared by the batch entry's failure paths. */
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

/* N disconnected same-color paths in ONE coverage cycle: clear the union
 * scratch once for the whole batch's bbox, sample every path's capsules,
 * composite once. Thousands of single-path calls would each pay a clear +
 * composite (dense lane charts marshal 30k+ paths per frame). */
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
    zgl_flush(); /* painter order before clearing/reusing scratch */
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        /* Resource failure only, not a rasterization workaround. */
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
    gl.Disable(ZGL_SCISSOR_TEST); /* normal clipping travels in vertices */
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
            if (dx * dx + dy * dy < 0.0001f) continue; /* CPU degenerate policy */
            gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
        }
        off += n * 2;
    }
    zgl_flush(); /* includes all segments across allocation-pressure flushes */
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush(); /* finish sampling before scratch is overwritten/released */
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
        if (t->cov_fbo) gl.DeleteFramebuffers(1, &t->cov_fbo);
        if (t->cov_tex) gl.DeleteTextures(1, &t->cov_tex);
    }
    if (g_zgl.target == s) g_zgl.target = NULL;
    free(t->shadow);
    memset(t, 0, sizeof(*t));
}

static void gl_drop_tex(unsigned int tex) {
    if (g_gl_state > 0 && tex) {
        if (g_zgl.sprite_tex == tex) {
            zgl_flush();
            g_zgl.sprite_tex = 0;
            g_zgl.sprite_handle = 0;
        }
        zan_gl_ctx_make_current();
        gl.DeleteTextures(1, &tex);
    }
}

/* ---- textured sprite batch (A356 P0) -----------------------------------*/

/* Upload the handle's image once; the texture lives in the sprite registry
 * entry, so decode-cache eviction + re-decode does not re-create it. */
static zgl_uint zgl_sprite_tex(int handle) {
    zan_sprite_t *sp = zan_sprite_get(handle);
    zan_img_t *img;
    zgl_uint tex;
    if (!sp) return 0;
    if (sp->tex) return (zgl_uint)sp->tex;
    img = zan_img_load(sp->key);
    if (!img) return 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(ZGL_TEXTURE_2D, tex);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_LINEAR);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
    /* zan_img_t holds ARGB32 (a<<24|r<<16|g<<8|b, memory B,G,R,A); GL wants
     * bytes in sampling order -- BGRA reads the same memory straight across. */
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, img->w, img->h, 0, ZGL_BGRA,
                  ZGL_UNSIGNED_BYTE, img->pix);
    if (gl.GetError() != ZGL_NO_ERROR) { gl.DeleteTextures(1, &tex); return 0; }
    sp->tex = (unsigned int)tex;
    return tex;
}

static void zgl_sprite_batch(zan_surface_t *s, int handle,
                             const float *quads, int count) {
    zan_sprite_t *sp;
    zan_img_t *img;
    zgl_uint tex;
    int iw, ih, i;
    tex = zgl_sprite_tex(handle);
    if (!tex) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        /* GPU cannot take the primitive (no target / dead context): the
         * surface's pixels are the truth, same seam the shape entries use. */
        cpu_sprite_batch(s, handle, quads, count);
        return;
    }
    if (g_zgl.sprite_handle != handle || g_zgl.sprite_tex != tex) {
        zgl_flush();             /* never switch atlases mid-batch */
        g_zgl.sprite_handle = handle;
        g_zgl.sprite_tex = tex;
    }
    sp = zan_sprite_get(handle);
    img = zan_img_load(sp->key);
    if (!img) return;
    iw = img->w; ih = img->h;
    for (i = 0; i < count; i++) {
        const float *q = quads + i * 10;
        u32 tint_raw;
        zgl_quad sq;
        float dx = q[0], dy = q[1], dw = q[2], dh = q[3];
        float sx = q[4], sy = q[5], sw = q[6], sh = q[7];
        memcpy(&tint_raw, q + 8, sizeof(u32));
        memset(&sq, 0, sizeof(sq));
        sq.kind = ZGL_K_SPRITE;
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

static const zan_gui_backend zan_gl_backend = {
    .name         = "gl",
    .clear_rect   = gl_clear_rect,
    .fill_rect    = gl_fill_rect,
    .fill_round   = gl_fill_round,
    .draw_round   = gl_draw_round,
    .surface_round = gl_surface_round,
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
    .polybatch    = gl_polybatch,
    .blur         = NULL,
    .snapshot     = NULL,
    .restore      = NULL,
    .draw_text    = NULL,
    .glyph_run    = gl_glyph_run,
    .blit_image   = NULL,
    .sprite_batch = zgl_sprite_batch,
    .mesh_create  = gl_mesh_create,
    .draw3d       = gl_draw3d,
    .set_clip     = gl_set_clip,
    .flush        = gl_flush,
    .read_pixels  = gl_read_pixels,
    .present      = gl_present,
    .drop_window  = gl_drop_window,
    .drop_surface = gl_drop_surface,
    .drop_tex     = gl_drop_tex,
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
    gl_mesh_drop_all();
}
