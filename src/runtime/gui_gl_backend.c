/* gui_gl_backend */

#include "gui_gl.h"
#include "gui_gl_context.c"

/* plumbing */

static zan_gl_api gl;
#define ZGL_MAX 0x8008
#define ZGL_FUNC_ADD 0x8006
static void (*zgl_blend_equation)(zgl_enum);
static int g_gl_state = 0;   /* 0 untried, 1 ready, -1 unusable */

#define ZGL_VF 34

typedef enum {
    ZGL_MODE_NONE = 0,
    ZGL_MODE_BLEND,     /* shapes, source-over */
    ZGL_MODE_REPLACE,
    ZGL_MODE_TEXT,
    ZGL_MODE_UNION
} zgl_mode;

#define ZGL_K_RECT    0
#define ZGL_K_CIRCLE  1
#define ZGL_K_RADIAL  2
#define ZGL_K_SECTOR  3
#define ZGL_K_CAPSULE 4
#define ZGL_K_TEXT1   5
#define ZGL_K_TEXT4   6
#define ZGL_K_UNION   7
#define ZGL_K_TEXTRGBA 9
#define ZGL_K_SURFACE 8
#define ZGL_K_BITMAP  10
#define ZGL_K_SHADOW  11

#define ZGL_TILE 64

typedef struct {
    zgl_uint tex, fbo;
    zgl_uint cov_tex, cov_fbo;
    zgl_uint snap_tex, snap_fbo;
    int snap_w, snap_h;
    int w, h;
    int gpu_ahead;
    int cpu_ahead;
    unsigned char *shadow;
    int shadow_valid;
} zgl_target;

static zgl_target g_zgl_targets[64];

static struct {
    zgl_uint prog_shape, prog_text, vao, vbo;
    zgl_int  u_viewport_shape, u_viewport_text, u_atlas1, u_atlas4, u_coverage,
             u_destination;
    zgl_uint atlas1, atlas4;
    zgl_int  u_atlas2;
    zgl_uint bitmap_tex;
    float   *verts;
    size_t   count, cap;             /* in floats */
    zgl_mode mode;
    zan_surface_t *target;
    int      is_gles;
} g_zgl;

#include "gui_gl_shaders.c"

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
        t->cpu_ahead = 1;
    }
    return t;
}

static void zgl_flush(void);

static void zgl_shadow_store(zan_surface_t *s, zgl_target *t,
                             int x, int y, int tw, int th) {
    size_t row = (size_t)tw * 4;
    for (int r = 0; r < th; r++)
        memcpy(t->shadow + ((size_t)(y + r) * (size_t)s->width + (size_t)x) * 4,
               s->pixels + (size_t)(y + r) * (size_t)s->stride + (size_t)x,
               row);
}

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

static void zgl_upload(zan_surface_t *s, zgl_target *t) {
    if (!t->cpu_ahead) return;
    gl.BindTexture(ZGL_TEXTURE_2D, t->tex);
    gl.PixelStorei(ZGL_UNPACK_ROW_LENGTH, 0);
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

static unsigned char *g_zgl_rb = NULL;
static size_t g_zgl_rb_cap = 0;

static void zgl_readback(zan_surface_t *s, zgl_target *t) {
    if (!t->gpu_ahead) return;
    if (s->width <= 0 || s->height <= 0) return;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    size_t row = (size_t)s->width * 4;
    size_t need = row * (size_t)s->height;
    if (need > g_zgl_rb_cap) {
        unsigned char *ng = (unsigned char *)realloc(g_zgl_rb, need);
        if (!ng) return;
        g_zgl_rb = ng;
        g_zgl_rb_cap = need;
    }
    gl.ReadPixels(0, 0, s->width, s->height, ZGL_BGRA, ZGL_UNSIGNED_BYTE,
                  g_zgl_rb);
    for (int y = 0; y < s->height; y++) {
        memcpy(s->pixels + (size_t)y * (size_t)s->stride,
               g_zgl_rb + row * (size_t)(s->height - 1 - y), row);
    }
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
    t->cpu_ahead = 1;
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
        if (g_zgl.is_gles) {
            gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                 ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        } else {
            gl.BlendFuncSeparate(ZGL_SRC1_COLOR, ZGL_ONE_MINUS_SRC1_COLOR,
                                 ZGL_SRC1_ALPHA, ZGL_ONE_MINUS_SRC1_ALPHA);
        }
    } else {
        gl.UseProgram(g_zgl.prog_shape);
        zgl_bind_attribs(g_zgl.prog_shape);
        gl.Uniform2f(g_zgl.u_viewport_shape, (float)t->w, (float)t->h);
        gl.ActiveTexture(ZGL_TEXTURE0);
        if (g_zgl.mode == ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, 0);
        } else if (shader_over) {
            if (snap_w != t->w || snap_h != t->h) {
                if (snap_tex) { gl.DeleteTextures(1, &snap_tex); gl.DeleteFramebuffers(1, &snap_fbo); snap_tex = snap_fbo = 0; }
                snap_w = snap_h = 0;
                gl.GenTextures(1, &snap_tex);
                gl.BindTexture(ZGL_TEXTURE_2D, snap_tex);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
                gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
                zgl_enum color_fmt = g_zgl.is_gles ? ZGL_RGBA : ZGL_BGRA;
                gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, t->w, t->h, 0,
                              color_fmt, ZGL_UNSIGNED_BYTE, NULL);
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
                shader_over = 0;
            }
        }
        if (g_zgl.mode != ZGL_MODE_UNION) {
            gl.BindTexture(ZGL_TEXTURE_2D, t->cov_tex);
            gl.Uniform1i(g_zgl.u_coverage, 0);
        }
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
            gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                 ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        }
    }

    gl.DrawArrays(ZGL_TRIANGLES, 0, (zgl_sizei)(g_zgl.count / ZGL_VF));
    g_zgl.count = 0;
    if (g_zgl.mode != ZGL_MODE_UNION) t->gpu_ahead = 1;
    zgl_blend_equation(ZGL_FUNC_ADD);
    gl.Enable(ZGL_BLEND);
}

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
    float cx, cy;
    float hw, hh;
    float radius, stroke;
    int   kind;
    float p0, p1, p2;          /* kind-specific */
    float col0[4], col1[4], col2[4];
    float seg[4];
    float u0, v0, u1, v1;
} zgl_quad;

static void zgl_color(float *out, u32 c, int force_opaque) {
    out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(c & 0xFF) / 255.0f;
    u32 a = (c >> 24) & 0xFF;
    out[3] = force_opaque ? 1.0f : (float)a / 255.0f;
}

static void zgl_push(zan_surface_t *s, const zgl_quad *q,
                     float x0, float y0, float x1, float y1) {
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
    float du = (q->u1 - q->u0), dv = (q->v1 - q->v0);
    float qw = (x1 - x0), qh = (y1 - y0);
    (void)qw; (void)qh;

    float inv_2hw = (q->hw > 0.0f) ? (1.0f / (2.0f * q->hw)) : 0.0f;
    float inv_2hh = (q->hh > 0.0f) ? (1.0f / (2.0f * q->hh)) : 0.0f;
    float min_x = q->cx - q->hw;
    float min_y = q->cy - q->hh;

    typedef struct { float d[4]; } zgl_v4;
    zgl_v4 c0 = *(const zgl_v4 *)q->col0;
    zgl_v4 c1 = *(const zgl_v4 *)q->col1;
    zgl_v4 c2 = *(const zgl_v4 *)q->col2;
    zgl_v4 sg = *(const zgl_v4 *)q->seg;
    float clip_x0 = (float)s->clip_x0, clip_y0 = (float)s->clip_y0;
    float clip_x1 = (float)s->clip_x1, clip_y1 = (float)s->clip_y1;
    float q_cx = q->cx, q_cy = q->cy;
    float q_hw = q->hw, q_hh = q->hh;
    float q_radius = q->radius, q_stroke = q->stroke;
    float q_kind = (float)q->kind, q_p0 = q->p0, q_p1 = q->p1, q_p2 = q->p2;

    for (int i = 0; i < 6; i++) {
        float *v = g_zgl.verts + g_zgl.count;
        float px = corner[i][0], py = corner[i][1];
        v[0] = px;            v[1] = py;
        v[2] = q_cx;          v[3] = q_cy;
        v[4] = q_hw;          v[5] = q_hh;
        v[6] = q_radius;      v[7] = q_stroke;
        v[8] = q_kind;        v[9] = q_p0; v[10] = q_p1; v[11] = q_p2;
        *(zgl_v4 *)(v + 12) = c0;
        *(zgl_v4 *)(v + 16) = c1;
        *(zgl_v4 *)(v + 20) = c2;
        v[24] = clip_x0;      v[25] = clip_y0;
        v[26] = clip_x1;      v[27] = clip_y1;
        *(zgl_v4 *)(v + 28) = sg;
        float fu = (inv_2hw > 0.0f) ? (px - min_x) * inv_2hw : 0.0f;
        float fv = (inv_2hh > 0.0f) ? (py - min_y) * inv_2hh : 0.0f;
        v[32] = q->u0 + du * fu;
        v[33] = q->v0 + dv * fv;
        g_zgl.count += ZGL_VF;
    }
}

/* primitives */

static void zgl_rect_quad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, int stroke, int gdir,
                          u32 c0, u32 c1, u32 via, int opaque) {
    float x0 = (float)x, y0 = (float)y;
    float x1 = (float)(x + w), y1 = (float)(y + h);
    if (x1 <= (float)s->clip_x0 || x0 >= (float)s->clip_x1 ||
        y1 <= (float)s->clip_y0 || y0 >= (float)s->clip_y1) {
        return;
    }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_RECT;
    float fw = (float)w, fh = (float)h;
    q.hw = fw * 0.5f;
    q.hh = fh * 0.5f;
    q.cx = x0 + q.hw;
    q.cy = y0 + q.hh;
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
    zgl_push(s, &q, x0, y0, x1, y1);
}

#include "gui_gl_draw.c"

/* text */

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
        return slot;
    }

    int shelf = (tile->bpp == 4) ? 1 : 0;
    if (g_zgl_shelf[shelf].x + tile->w > ZGL_ATLAS_DIM) {
        g_zgl_shelf[shelf].x = 0;
        g_zgl_shelf[shelf].y += g_zgl_shelf[shelf].row_h;
        g_zgl_shelf[shelf].row_h = 0;
    }
    if (g_zgl_shelf[shelf].y + tile->h > ZGL_ATLAS_DIM) {
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
    if (((color >> 24) & 0xFF) == 0) color |= 0xFF000000u;
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        zgl_tile_slot *slot = zgl_tile_upload(tile);
        if (!slot) continue;
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


static void gl_snapshot(zan_surface_t *s, int x, int y, int w, int h, int slot) {
    (void)x; (void)y; (void)w; (void)h; (void)slot;
    if (!s || g_gl_state <= 0) return;
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    if (g_zgl.target == s || g_zgl.count) zgl_flush();
    if (!zan_gl_ctx_make_current()) return;

    if (t->snap_w != t->w || t->snap_h != t->h || !t->snap_tex || !t->snap_fbo) {
        if (t->snap_fbo) gl.DeleteFramebuffers(1, &t->snap_fbo);
        if (t->snap_tex) gl.DeleteTextures(1, &t->snap_tex);
        t->snap_tex = t->snap_fbo = 0;
        t->snap_w = t->snap_h = 0;

        gl.GenTextures(1, &t->snap_tex);
        gl.BindTexture(ZGL_TEXTURE_2D, t->snap_tex);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
        gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, t->w, t->h, 0,
                      ZGL_BGRA, ZGL_UNSIGNED_BYTE, NULL);
        gl.GenFramebuffers(1, &t->snap_fbo);
        gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->snap_fbo);
        gl.FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                                ZGL_TEXTURE_2D, t->snap_tex, 0);
        if (gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) == ZGL_FRAMEBUFFER_COMPLETE) {
            t->snap_w = t->w;
            t->snap_h = t->h;
        } else {
            gl.DeleteFramebuffers(1, &t->snap_fbo);
            gl.DeleteTextures(1, &t->snap_tex);
            t->snap_tex = t->snap_fbo = 0;
            t->snap_w = t->snap_h = 0;
            gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
            return;
        }
    }

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->fbo);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, t->snap_fbo);
    gl.BlitFramebuffer(0, 0, t->w, t->h, 0, 0, t->w, t->h,
                       ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
}

static int gl_restore(zan_surface_t *s, int x, int y, int w, int h, int slot, int sub_rect) {
    (void)x; (void)y; (void)w; (void)h; (void)slot; (void)sub_rect;
    if (!s || g_gl_state <= 0) return 0;
    zgl_target *t = zgl_target_of(s);
    if (!t || !t->snap_fbo || !t->snap_tex) return 0;
    if (g_zgl.target == s || g_zgl.count) zgl_flush();
    if (!zan_gl_ctx_make_current()) return 0;

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->snap_fbo);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, t->fbo);
    gl.BlitFramebuffer(0, 0, t->w, t->h, 0, 0, t->w, t->h,
                       ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    t->gpu_ahead = 1;
    t->cpu_ahead = 0;
    return 1;
}

static void gl_set_clip(zan_surface_t *s, int x0, int y0, int x1, int y1) {
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

static int gl_present(zan_surface_t *s, void *native_window) {
    if (g_gl_state <= 0 || !native_window) return 0;
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    zgl_upload(s, t);
    if (!zan_gl_ctx_present_begin(native_window, t->w, t->h)) return 0;

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, t->fbo);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, 0);
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
        if (t->snap_fbo) gl.DeleteFramebuffers(1, &t->snap_fbo);
        if (t->snap_tex) gl.DeleteTextures(1, &t->snap_tex);
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

#define ZGL_BITMAP_CAP 256
#define ZGL_BITMAP_BYTES (64u * 1024u * 1024u)

// The budget of the picture cache.  An application that draws pictures much
// bigger than the default budget -- a tileset of a high definition game, for
// instance -- raises it with the environment variable ZAN_GUI_BITMAP_MB.  A
// picture that does not fit into the budget is still drawn, but it leaves no
// room for the other pictures and the cache starts to load them again and
// again.
//
// The variable is read through the system of the platform first: a program
// that sets it while it runs (through SetEnvironmentVariable of the runtime,
// which changes the environment of the process but not the copy the C library
// keeps) is only seen there.  The C library is the second try.
static size_t zgl_bitmap_budget(void) {
    static size_t budget = 0;
    if (budget == 0) {
        budget = ZGL_BITMAP_BYTES;
        long v = 0;
#if defined(_WIN32)
        char envbuf[32];
        DWORD r = GetEnvironmentVariableA("ZAN_GUI_BITMAP_MB", envbuf, (DWORD)sizeof(envbuf));
        if (r > 0 && r < sizeof(envbuf)) v = strtol(envbuf, NULL, 10);
#endif
        if (v <= 0) {
            const char *mb = getenv("ZAN_GUI_BITMAP_MB");
            if (mb && *mb) v = strtol(mb, NULL, 10);
        }
        if (v > 0) budget = (size_t)v * 1024u * 1024u;
    }
    return budget;
}
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
    while (g_bitmap_bytes && g_bitmap_bytes + bytes > zgl_bitmap_budget()) {
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
    if (g_zgl.is_gles) {
        /* ES 核心没有 BGRA 上传格式：按 RGBA 读入（字节序 B,G,R,A 落成
         * R=B、B=R），再用纹理分量交换把红蓝换回。桌面 GL 直接 BGRA。 */
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_SWIZZLE_R, ZGL_BLUE);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_SWIZZLE_G, ZGL_GREEN);
        gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_SWIZZLE_B, ZGL_RED);
        gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, img->width, img->height, 0,
                      ZGL_RGBA, ZGL_UNSIGNED_BYTE, img->pixels);
    } else {
        gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, img->width, img->height, 0,
                      ZGL_BGRA, ZGL_UNSIGNED_BYTE, img->pixels);
    }
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
    float inv_iw = (iw > 0) ? (1.0f / (float)iw) : 0.0f;
    float inv_ih = (ih > 0) ? (1.0f / (float)ih) : 0.0f;
    float fiw = (float)iw, fih = (float)ih;
    float clip_x0 = (float)s->clip_x0, clip_y0 = (float)s->clip_y0;
    float clip_x1 = (float)s->clip_x1, clip_y1 = (float)s->clip_y1;

    zgl_quad sq;
    memset(&sq, 0, sizeof(sq));
    sq.kind = ZGL_K_BITMAP;

    for (i = 0; i < count; i++) {
        const float *q = quads + i * 10;
        float dx = q[0], dy = q[1], dw = q[2], dh = q[3];
        if (dw <= 0.0f) dw = fiw;
        if (dh <= 0.0f) dh = fih;
        float r_x1 = dx + dw, r_y1 = dy + dh;
        if (r_x1 <= clip_x0 || dx >= clip_x1 || r_y1 <= clip_y0 || dy >= clip_y1) {
            continue;
        }

        float sx = q[4], sy = q[5], sw = q[6], sh = q[7];
        u32 tint_raw = *(const u32 *)(q + 8);
        if (sw <= 0.0f) { sx = 0.0f; sw = fiw; }
        if (sh <= 0.0f) { sy = 0.0f; sh = fih; }
        sq.cx = dx + dw * 0.5f;
        sq.cy = dy + dh * 0.5f;
        sq.hw = dw * 0.5f;
        sq.hh = dh * 0.5f;
        sq.u0 = sx * inv_iw;
        sq.v0 = sy * inv_ih;
        sq.u1 = (sx + sw) * inv_iw;
        sq.v1 = (sy + sh) * inv_ih;
        zgl_color(sq.col0, tint_raw, 0);
        zgl_push(s, &sq, dx, dy, r_x1, r_y1);
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
    .shadow_round = gl_shadow_round,
    .fill_circle  = gl_fill_circle,
    .draw_circle  = gl_draw_circle,
    .fill_radial  = gl_fill_radial,
    .fill_sector  = gl_fill_sector,
    .draw_line    = gl_draw_line,
    .polyline     = gl_polyline,
    .polybatch    = gl_polybatch,
    .blur         = NULL,
    .snapshot     = gl_snapshot,
    .restore      = gl_restore,
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

int zan_gui_internal_gl_install(void) {
    if (!zgl_init()) return 0;
    zan_gui_internal_set_backend(&zan_gl_backend);
    return 1;
}

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
