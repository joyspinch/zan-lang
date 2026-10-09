#include "zan_game.h"
#include "zan_gui_graphics.h"
#include "zan_image.h"

#include <stddef.h>
#include <string.h>

#define ZAN_GAME_SPRITE_CAP 4096
#define ZAN_GAME_KEY_CAP 512
#define ZAN_GAME_QUAD_FLOATS 10
#define ZAN_GAME_MESH_CAP 24
#define ZAN_GAME_SURFACE_CAP 64
#define ZAN_GAME_VERTEX_FLOATS 8
#define ZAN_GAME_VERTEX_CAP 65536
#define ZAN_GAME_GL_ERROR_LIMIT 16

/* 编译器代码生成与运行时系统底层调用契约 */
typedef struct {
    char key[ZAN_GAME_KEY_CAP];
} zan_game_sprite;

static zan_game_sprite g_game_sprites[ZAN_GAME_SPRITE_CAP];
static int g_game_sprite_count;

static int32_t zan_game_find_sprite(const char *key) {
    for (int i = 0; i < g_game_sprite_count; ++i)
        if (strcmp(g_game_sprites[i].key, key) == 0) return i + 1;
    return 0;
}

static int32_t zan_game_store_sprite(const char *key) {
    int32_t handle = zan_game_find_sprite(key);
    if (handle) return handle;
    if (g_game_sprite_count >= ZAN_GAME_SPRITE_CAP) return 0;
    size_t len = strlen(key);
    if (len >= ZAN_GAME_KEY_CAP) return 0;
    memcpy(g_game_sprites[g_game_sprite_count].key, key, len + 1);
    return ++g_game_sprite_count;
}

static int zan_game_bitmap_valid(const zan_bitmap *bitmap) {
    return bitmap && bitmap->pixels && bitmap->width > 0 &&
        bitmap->height > 0 && bitmap->stride >= bitmap->width &&
        (size_t)bitmap->height <= SIZE_MAX / sizeof(*bitmap->pixels) /
            (size_t)bitmap->stride;
}

ZAN_GAME_API int32_t zan_game_sprite_handle(const char *key) {
    if (!key || !key[0] || strlen(key) >= ZAN_GAME_KEY_CAP) return 0;
    int32_t handle = zan_game_find_sprite(key);
    if (handle) return handle;
    if (g_game_sprite_count >= ZAN_GAME_SPRITE_CAP ||
        !zan_game_bitmap_valid(zan_image_get(key))) return 0;
    return zan_game_store_sprite(key);
}

ZAN_GAME_API int32_t zan_game_bake_sprite(const char *key, int32_t surface_id,
    int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!key || !key[0]) return 0;
    char full_key[ZAN_GAME_KEY_CAP];
    size_t len = strlen(key);
    size_t prefix = strncmp(key, "mem:", 4) == 0 ? 0 : 4;
    /* 底层系统交互与数据协议契约 */
    if (len >= sizeof(full_key) - prefix) return 0;
    if (prefix) memcpy(full_key, "mem:", prefix);
    memcpy(full_key + prefix, key, len + 1);
    if (!zan_game_find_sprite(full_key) &&
        g_game_sprite_count >= ZAN_GAME_SPRITE_CAP) return 0;

    const zan_bitmap *source = zan_gui_surface_bitmap(surface_id);
    if (!zan_game_bitmap_valid(source)) return 0;
    int64_t bx = x, by = y;
    int64_t bw = width > 0 ? width : source->width;
    int64_t bh = height > 0 ? height : source->height;
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx >= source->width || by >= source->height) return 0;
    if (bw > source->width - bx) bw = source->width - bx;
    if (bh > source->height - by) bh = source->height - by;
    if (bw <= 0 || bh <= 0) return 0;

    const uint32_t *slice = source->pixels + (size_t)by *
        (size_t)source->stride + (size_t)bx;
    /* 编译器代码生成与运行时系统底层调用契约 */
    if (zan_image_register_argb(full_key, slice, (int)bw, (int)bh,
                               source->stride) <= 0) return 0;
    return zan_game_store_sprite(full_key);
}

ZAN_GAME_API void zan_game_sprite_batch(int32_t surface_id, int32_t handle,
    const float *quads, int32_t count) {
    if (!quads || count <= 0 || handle <= 0 ||
        handle > g_game_sprite_count ||
        (size_t)count > (size_t)PTRDIFF_MAX /
            (ZAN_GAME_QUAD_FLOATS * sizeof(*quads))) return;
    const zan_bitmap *bitmap = zan_image_get(g_game_sprites[handle - 1].key);
    if (!zan_game_bitmap_valid(bitmap)) return;
    zan_gui_blit_pixels_batch(surface_id, bitmap, quads, count);
}

typedef struct {
    zgl_uint vao, vbo, ebo;
    int32_t id;
    int index_count;
    uint64_t epoch;
} zan_game_mesh;

typedef struct {
    zgl_uint renderbuffer;
    int width, height;
} zan_game_depth;

static zan_game_mesh g_game_meshes[ZAN_GAME_MESH_CAP];
static zan_game_depth g_game_depth[ZAN_GAME_SURFACE_CAP];
static const zan_gl_api *g_game_gl;
static uint64_t g_game_epoch;
static zgl_uint g_game_program;
static zgl_int g_game_u_mvp = -1, g_game_u_tex = -1, g_game_u_color = -1;
static zgl_int g_game_a_pos = -1, g_game_a_normal = -1, g_game_a_uv = -1;
static int g_game_cleanup_registered;
/* 编译器代码生成与运行时系统底层调用契约 */
static uint32_t g_game_next_mesh_id = 1;

static const uint32_t g_game_white_pixel = UINT32_C(0xFFFFFFFF);
/* 编译器代码生成与运行时系统底层调用契约 */
static const zan_bitmap g_game_white = {
    &g_game_white_pixel, 1, 1, 1, UINT64_C(1)
};

static const char *const ZAN_GAME_3D_VS =
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
/* 编译器代码生成与运行时系统底层调用契约 */
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

static const char *const ZAN_GAME_3D_FS =
"#version 330 core\n"
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"out vec4 frag;\n"
"void main() {\n"
/* 编译器代码生成与运行时系统底层调用契约 */
"    float lam = clamp(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.6))) * 0.5 + 0.5, 0.0, 1.0);\n"
"    float shade = 0.55 + 0.45 * lam;\n"
"    vec4 tex = texture(uTex, v_uv);\n"
"    frag = vec4(tex.rgb * uColor.rgb * shade, tex.a * uColor.a);\n"
"}\n";

static void zan_game_drop_mesh(zan_game_mesh *mesh) {
    if (mesh->vao) g_game_gl->DeleteVertexArrays(1, &mesh->vao);
    if (mesh->vbo) g_game_gl->DeleteBuffers(1, &mesh->vbo);
    if (mesh->ebo) g_game_gl->DeleteBuffers(1, &mesh->ebo);
    memset(mesh, 0, sizeof(*mesh));
}

static void zan_game_drop_depth(zan_game_depth *depth) {
    if (depth->renderbuffer)
        g_game_gl->DeleteRenderbuffers(1, &depth->renderbuffer);
    memset(depth, 0, sizeof(*depth));
}

static void zan_game_forget_gpu(void) {
    memset(g_game_meshes, 0, sizeof(g_game_meshes));
    memset(g_game_depth, 0, sizeof(g_game_depth));
    g_game_program = 0;
    g_game_u_mvp = g_game_u_tex = g_game_u_color = -1;
    g_game_a_pos = g_game_a_normal = g_game_a_uv = -1;
    g_game_epoch = 0;
    g_game_gl = NULL;
}

static void zan_game_gpu_cleanup(int32_t surface_id) {
    /* 编译器代码生成与运行时系统底层调用契约 */
    if (surface_id >= 0) {
        if (surface_id < ZAN_GAME_SURFACE_CAP && g_game_gl)
            zan_game_drop_depth(&g_game_depth[surface_id]);
        return;
    }
    if (surface_id != -1) return;
    if (g_game_gl) {
        for (int i = 0; i < ZAN_GAME_MESH_CAP; ++i)
            zan_game_drop_mesh(&g_game_meshes[i]);
        for (int i = 0; i < ZAN_GAME_SURFACE_CAP; ++i)
            zan_game_drop_depth(&g_game_depth[i]);
        if (g_game_program) g_game_gl->DeleteProgram(g_game_program);
    }
    zan_game_forget_gpu();
}

static int zan_game_gpu_prepare(const zan_gpu_frame *frame) {
    if (!frame->api || !frame->framebuffer || frame->width <= 0 ||
        frame->height <= 0) return 0;
    if (g_game_gl && g_game_epoch != frame->epoch) {
        /* 编译器代码生成与运行时系统底层调用契约 */
        zan_game_forget_gpu();
    }
    g_game_gl = frame->api;
    g_game_epoch = frame->epoch;
    if (!g_game_cleanup_registered) {
        if (!zan_gui_gpu_register_cleanup(zan_game_gpu_cleanup)) return 0;
        g_game_cleanup_registered = 1;
    }
    /* 编译器代码生成与运行时系统底层调用契约 */
    for (int i = 0; i < ZAN_GAME_GL_ERROR_LIMIT; ++i)
        if (g_game_gl->GetError() == ZGL_NO_ERROR) return 1;
    return 0;
}

static int zan_game_program_ready(void) {
    if (g_game_program) return 1;
    zgl_uint program = zan_gui_gpu_program(ZAN_GAME_3D_VS, ZAN_GAME_3D_FS);
    if (!program) return 0;
    zgl_int mvp = g_game_gl->GetUniformLocation(program, "uMVP");
    zgl_int tex = g_game_gl->GetUniformLocation(program, "uTex");
    zgl_int color = g_game_gl->GetUniformLocation(program, "uColor");
    zgl_int pos = g_game_gl->GetAttribLocation(program, "a_pos");
    zgl_int normal = g_game_gl->GetAttribLocation(program, "a_normal");
    zgl_int uv = g_game_gl->GetAttribLocation(program, "a_uv");
    if (mvp < 0 || tex < 0 || color < 0 || pos < 0 || normal < 0 || uv < 0 ||
        g_game_gl->GetError() != ZGL_NO_ERROR) {
        g_game_gl->DeleteProgram(program);
        return 0;
    }
    g_game_program = program;
    g_game_u_mvp = mvp; g_game_u_tex = tex; g_game_u_color = color;
    g_game_a_pos = pos; g_game_a_normal = normal; g_game_a_uv = uv;
    return 1;
}

static zan_game_mesh *zan_game_find_mesh(int32_t id, uint64_t epoch) {
    for (int i = 0; i < ZAN_GAME_MESH_CAP; ++i)
        if (g_game_meshes[i].id == id && g_game_meshes[i].epoch == epoch)
            return &g_game_meshes[i];
    return NULL;
}

ZAN_GAME_API int32_t zan_game_mesh_create(int32_t surface_id, const float *verts,
    int32_t count, const unsigned short *indices, int32_t index_count) {
    if (!verts || !indices || count <= 0 || count > ZAN_GAME_VERTEX_CAP ||
        index_count <= 0 || g_game_next_mesh_id > INT32_MAX ||
        (size_t)index_count > (size_t)PTRDIFF_MAX / sizeof(*indices)) return 0;
    for (int32_t i = 0; i < index_count; ++i)
        if ((uint32_t)indices[i] >= (uint32_t)count) return 0;

    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_mesh *mesh = NULL;
    for (int i = 0; i < ZAN_GAME_MESH_CAP; ++i)
        if (!g_game_meshes[i].id) { mesh = &g_game_meshes[i]; break; }
    if (!mesh || !zan_game_program_ready()) goto done;

    const zan_gl_api *gl = frame.api;
    gl->GenVertexArrays(1, &mesh->vao);
    if (!mesh->vao) goto failed;
    gl->BindVertexArray(mesh->vao);
    gl->GenBuffers(1, &mesh->vbo);
    if (!mesh->vbo) goto failed;
    gl->BindBuffer(ZGL_ARRAY_BUFFER, mesh->vbo);
    gl->BufferData(ZGL_ARRAY_BUFFER,
        (zgl_sizeiptr)((size_t)count * ZAN_GAME_VERTEX_FLOATS * sizeof(*verts)),
        verts, ZGL_STATIC_DRAW);
    gl->GenBuffers(1, &mesh->ebo);
    if (!mesh->ebo) goto failed;
    gl->BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    gl->BufferData(ZGL_ELEMENT_ARRAY_BUFFER,
        (zgl_sizeiptr)((size_t)index_count * sizeof(*indices)), indices,
        ZGL_STATIC_DRAW);
    const zgl_int locations[] = {
        g_game_a_pos, g_game_a_normal, g_game_a_uv
    };
    const int sizes[] = { 3, 3, 2 };
    const size_t offsets[] = { 0, 3 * sizeof(float), 6 * sizeof(float) };
    for (int i = 0; i < 3; ++i) {
        gl->EnableVertexAttribArray((zgl_uint)locations[i]);
        gl->VertexAttribPointer((zgl_uint)locations[i], sizes[i], ZGL_FLOAT,
            ZGL_FALSE, ZAN_GAME_VERTEX_FLOATS * (zgl_sizei)sizeof(float),
            (const void *)offsets[i]);
    }
    if (gl->GetError() != ZGL_NO_ERROR) goto failed;
    mesh->index_count = index_count;
    mesh->epoch = frame.epoch;
    mesh->id = (int32_t)g_game_next_mesh_id++;
    result = mesh->id;
    gl->BindVertexArray(0);
    goto done;

failed:
    gl->BindVertexArray(0);
    zan_game_drop_mesh(mesh);
done:
    zan_gui_gpu_end(surface_id);
    return result;
}

static int zan_game_depth_ready(int32_t surface_id, const zan_gpu_frame *frame) {
    zan_game_depth *depth = &g_game_depth[surface_id];
    const zan_gl_api *gl = frame->api;
    if (depth->renderbuffer && (depth->width != frame->width ||
                               depth->height != frame->height))
        zan_game_drop_depth(depth);
    if (!depth->renderbuffer) {
        gl->GenRenderbuffers(1, &depth->renderbuffer);
        if (!depth->renderbuffer) return 0;
        gl->BindRenderbuffer(ZGL_RENDERBUFFER, depth->renderbuffer);
        gl->RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH_COMPONENT16,
                                frame->width, frame->height);
        depth->width = frame->width;
        depth->height = frame->height;
    }
    /* 编译器代码生成与运行时系统底层调用契约 */
    gl->FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_DEPTH_ATTACHMENT,
                               ZGL_RENDERBUFFER, depth->renderbuffer);
    if (gl->CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE ||
        gl->GetError() != ZGL_NO_ERROR) {
        zan_game_drop_depth(depth);
        return 0;
    }
    return 1;
}

static void zan_game_clip(const zan_gpu_frame *frame, int *x, int *y,
                          int *width, int *height) {
    int64_t x0 = frame->clip_x, y0 = frame->clip_y;
    int64_t x1 = x0 + frame->clip_w, y1 = y0 + frame->clip_h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > frame->width) x1 = frame->width;
    if (y1 > frame->height) y1 = frame->height;
    if (frame->clip_w <= 0 || frame->clip_h <= 0 || x1 <= x0 || y1 <= y0) {
        *x = *y = *width = *height = 0;
        return;
    }
    *x = (int)x0;
    *y = frame->height - (int)y1;
    *width = (int)(x1 - x0);
    *height = (int)(y1 - y0);
}

ZAN_GAME_API int32_t zan_game_draw3d(int32_t surface_id, int32_t mesh_id,
    const float *mvp, int32_t color, const char *texture) {
    if (!mvp || mesh_id <= 0 || surface_id < 0 ||
        surface_id >= ZAN_GAME_SURFACE_CAP) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_mesh *mesh = zan_game_find_mesh(mesh_id, frame.epoch);
    if (!mesh || !g_game_program) goto done;
    int clip_x, clip_y, clip_w, clip_h;
    zan_game_clip(&frame, &clip_x, &clip_y, &clip_w, &clip_h);
    if (!clip_w || !clip_h) { result = 1; goto done; }

    const zan_bitmap *bitmap = texture && texture[0] ? zan_image_get(texture) : NULL;
    if (!zan_game_bitmap_valid(bitmap)) bitmap = &g_game_white;
    zgl_uint tex = zan_gui_gpu_texture(bitmap);
    if (!tex && bitmap != &g_game_white) tex = zan_gui_gpu_texture(&g_game_white);
    if (!tex) goto done;

    const zan_gl_api *gl = frame.api;
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    if (!zan_game_depth_ready(surface_id, &frame)) goto done;
    gl->Viewport(0, 0, frame.width, frame.height);
    gl->Enable(ZGL_SCISSOR_TEST);
    gl->Scissor(clip_x, clip_y, clip_w, clip_h);
    /* 底层系统交互与数据协议契约 */
    gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    gl->Disable(ZGL_BLEND);
    gl->UseProgram(g_game_program);
    gl->UniformMatrix4fv(g_game_u_mvp, 1, ZGL_FALSE, mvp);
    uint32_t argb = (uint32_t)color;
    gl->Uniform4f(g_game_u_color,
        (float)((argb >> 16) & 255) / 255.0f,
        (float)((argb >> 8) & 255) / 255.0f,
        (float)(argb & 255) / 255.0f,
        (float)((argb >> 24) & 255) / 255.0f);
    gl->ActiveTexture(ZGL_TEXTURE0);
    gl->BindTexture(ZGL_TEXTURE_2D, tex);
    gl->Uniform1i(g_game_u_tex, 0);
    gl->BindVertexArray(mesh->vao);
    gl->DrawElements(ZGL_TRIANGLES, mesh->index_count, ZGL_UNSIGNED_SHORT, NULL);
    gl->BindVertexArray(0);
    /* 编译器代码生成与运行时系统底层调用契约 */
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}
