#include "zan_game.h"
#include "zan_gui_graphics.h"
#include "zan_image.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZAN_GAME_SPRITE_CAP 4096
#define ZAN_GAME_KEY_CAP 512
#define ZAN_GAME_QUAD_FLOATS 10
#define ZAN_GAME_MESH_INIT_CAP 256
#define ZAN_GAME_MESH_MAX_CAP 65536
#define ZAN_GAME_SURFACE_CAP 64
#define ZAN_GAME_VERTEX_FLOATS 8
#define ZAN_GAME_COLORED_VERTEX_FLOATS 12
#define ZAN_GAME_VERTEX_CAP 65536
#define ZAN_GAME_GL_ERROR_LIMIT 16
/* 实例记录：mvp 16 个 float（列主序）+ tint rgba 4 个 float（线性 0..1）。 */
#define ZAN_GAME_INSTANCE_FLOATS 20
#define ZAN_GAME_INSTANCE_CAP 1024

typedef struct {
    char key[ZAN_GAME_KEY_CAP];
} zan_game_sprite;

static zan_game_sprite g_game_sprites[ZAN_GAME_SPRITE_CAP];
static int g_game_sprite_count;
static int g_game_last_sprite_idx = -1;

static int32_t zan_game_find_sprite(const char *key) {
    if (!key) return 0;
    if (g_game_last_sprite_idx >= 0 && g_game_last_sprite_idx < g_game_sprite_count) {
        if (strcmp(g_game_sprites[g_game_last_sprite_idx].key, key) == 0)
            return g_game_last_sprite_idx + 1;
    }
    for (int i = 0; i < g_game_sprite_count; ++i) {
        if (strcmp(g_game_sprites[i].key, key) == 0) {
            g_game_last_sprite_idx = i;
            return i + 1;
        }
    }
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
    int colored;      /* 顶点带 a_color（12 float/顶点） */
    int inst_ready;   /* 本 VAO 已配置实例属性（divisor=1） */
    uint64_t epoch;
} zan_game_mesh;

typedef struct {
    zgl_uint renderbuffer;
    int width, height;
} zan_game_depth;

/* 着色器程序按 [colored][instanced] 惰性建，属性位置全部显式 layout，
 * 同一网格 VAO 可跨程序复用。u_mvp/u_color 仅非实例化程序使用。
 * 光照 uniform 全局共享（g_game_light_*），按版本号惰性上传：程序内
 * uniform 状态跨调用保持，只有 set_lights3d 递增版本后才重传，批量
 * 通路中途换光照也能在下一次提交时生效。 */
typedef struct {
    zgl_uint program;
    zgl_int u_mvp, u_tex, u_color;
    zgl_int u_light_dir, u_light_color, u_light_count, u_ambient;
    uint32_t light_version;
} zan_game_program;

#define ZAN_GAME_LOC_POS 0
#define ZAN_GAME_LOC_NORMAL 1
#define ZAN_GAME_LOC_UV 2
#define ZAN_GAME_LOC_COLOR 3
#define ZAN_GAME_LOC_IMVP 4   /* mat4 占用 4..7 */
#define ZAN_GAME_LOC_ITINT 8

/* 3D 光照状态（A489）。缺省一盏键光还原历史固定光照：
 * shade = ambient + color*halfLambert = 0.55 + 0.45*lam，方向
 * (0.4,0.8,0.6) 与旧着色器常量逐位相同（着色器内归一化，这里不预归一）。 */
#define ZAN_GAME_MAX_LIGHTS 4
static float g_game_light_dir[ZAN_GAME_MAX_LIGHTS][3] = {
    { 0.4f, 0.8f, 0.6f }, { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
};
static float g_game_light_color[ZAN_GAME_MAX_LIGHTS][3] = {
    { 0.45f, 0.45f, 0.45f }, { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
};
static int g_game_light_count = 1;
static float g_game_ambient[3] = { 0.55f, 0.55f, 0.55f };
static uint32_t g_game_light_version = 1;

static zan_game_mesh *g_game_meshes = NULL;
static int g_game_mesh_cap = 0;
static zan_game_depth g_game_depth[ZAN_GAME_SURFACE_CAP];
static const zan_gl_api *g_game_gl;
static uint64_t g_game_epoch;
static zan_game_program g_game_progs[2][2];
static zgl_uint g_game_instance_vbo;
static int32_t g_game_batch_surface = -1;
static zan_gpu_frame g_game_batch_frame;
static int g_game_batch_clip_empty;
static zgl_uint g_game_batch_prog = 0;
static zgl_uint g_game_batch_tex = 0;
static int g_game_cleanup_registered;
static uint32_t g_game_next_mesh_id = 1;

static const uint32_t g_game_white_pixel = UINT32_C(0xFFFFFFFF);
static const zan_bitmap g_game_white = {
    &g_game_white_pixel, 1, 1, 1, UINT64_C(1)
};

/* GLSL 方言头：桌面 GL 用 330 core；GLES（Android/OHOS/EGL）用 300 es，
 * 片元着色器没有默认浮点精度，必须补 highp 声明。判别式与 gui_gl_backend
 * 的 2D 程序一致（GL_VERSION 含 "OpenGL ES"；安卓/鸿蒙编译期即真）。
 * 下面的着色器常量本体只放方言无关部分，建程序时拼上头。 */
static const char ZAN_GAME_GLSL_HEAD_DESKTOP[] = "#version 330 core\n";
static const char ZAN_GAME_GLSL_HEAD_GLES[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n";

static const char *zan_game_glsl_head(void) {
#if defined(__ANDROID__) || defined(__OHOS__)
    return ZAN_GAME_GLSL_HEAD_GLES;
#else
    if (!g_game_gl || !g_game_gl->GetString) return ZAN_GAME_GLSL_HEAD_DESKTOP;
    const char *ver = (const char *)g_game_gl->GetString(ZGL_VERSION);
    return (ver && strstr(ver, "OpenGL ES"))
               ? ZAN_GAME_GLSL_HEAD_GLES : ZAN_GAME_GLSL_HEAD_DESKTOP;
#endif
}

/* 属性位置常量与 VAO 配置一一对应（ZAN_GAME_LOC_*），四种变体共享同一
 * 网格 VAO。Y 翻转：3D 画进画布 FBO（顶朝下表面），clip 空间先翻 Y。 */

static const char *const ZAN_GAME_3D_VS_PLAIN =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"uniform mat4 uMVP;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"void main() {\n"
"    v_uv = a_uv;\n"
"    v_normal = a_normal;\n"
"    vec4 clip = uMVP * vec4(a_pos, 1.0);\n"
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_COLOR =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"layout(location = 3) in vec4 a_color;\n"
"uniform mat4 uMVP;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec4 v_color;\n"
"void main() {\n"
"    v_uv = a_uv;\n"
"    v_normal = a_normal;\n"
"    v_color = a_color;\n"
"    vec4 clip = uMVP * vec4(a_pos, 1.0);\n"
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_INST =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"layout(location = 4) in mat4 i_mvp;\n"
"layout(location = 8) in vec4 i_tint;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec4 v_tint;\n"
"void main() {\n"
"    v_uv = a_uv;\n"
"    v_normal = a_normal;\n"
"    v_tint = i_tint;\n"
"    vec4 clip = i_mvp * vec4(a_pos, 1.0);\n"
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_INST_COLOR =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"layout(location = 3) in vec4 a_color;\n"
"layout(location = 4) in mat4 i_mvp;\n"
"layout(location = 8) in vec4 i_tint;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec4 v_color;\n"
"out vec4 v_tint;\n"
"void main() {\n"
"    v_uv = a_uv;\n"
"    v_normal = a_normal;\n"
"    v_color = a_color;\n"
"    v_tint = i_tint;\n"
"    vec4 clip = i_mvp * vec4(a_pos, 1.0);\n"
"    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);\n"
"}\n";

/* 光照 uniform（A489）：四份片元着色器共享同一段光照计算——环境光 +
 * 至多 4 盏方向光的半兰伯特贡献，方向与颜色走 uniform 数组（实例化
 * 批次每实例只带 20 float，光源塞不进实例属性，只能每批次随程序提交）。
 * 缺省值（g_game_light_* 初始化）恰好还原历史固定键光：
 * 0.55 + 0.45 * halfLambert(normal, (0.4,0.8,0.6))，逐位一致。
 * 循环上界写常量 4、体内 break——GLSL ES 3.00 的循环条件要求常量
 * 表达式，动态盏数只能在体内截断。共享段以宏拼进各着色器字面量
 * （C 不允许 char[] 变量参与字符串字面量拼接）。 */
#define ZAN_GAME_3D_LIGHT_UNIFORMS \
"uniform vec3 uLightDir[4];\n" \
"uniform vec3 uLightColor[4];\n" \
"uniform int uLightCount;\n" \
"uniform vec3 uAmbient;\n"

#define ZAN_GAME_3D_LIGHT_BODY \
"    vec3 light = uAmbient;\n" \
"    for (int i = 0; i < 4; ++i) {\n" \
"        if (i >= uLightCount) break;\n" \
"        float lam = clamp(dot(normalize(v_normal), normalize(uLightDir[i])) * 0.5 + 0.5, 0.0, 1.0);\n" \
"        light += uLightColor[i] * lam;\n" \
"    }\n"

static const char *const ZAN_GAME_3D_FS_PLAIN =
ZAN_GAME_3D_LIGHT_UNIFORMS
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_LIGHT_BODY
"    vec4 tex = texture(uTex, v_uv);\n"
"    frag = vec4(tex.rgb * uColor.rgb * light, tex.a * uColor.a);\n"
"}\n";

static const char *const ZAN_GAME_3D_FS_COLOR =
ZAN_GAME_3D_LIGHT_UNIFORMS
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"in vec4 v_color;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_LIGHT_BODY
"    vec4 tex = texture(uTex, v_uv);\n"
"    vec3 rgb = tex.rgb * uColor.rgb * v_color.rgb * light;\n"
"    frag = vec4(rgb, tex.a * uColor.a * v_color.a);\n"
"}\n";

static const char *const ZAN_GAME_3D_FS_INST =
ZAN_GAME_3D_LIGHT_UNIFORMS
"uniform sampler2D uTex;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"in vec4 v_tint;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_LIGHT_BODY
"    vec4 tex = texture(uTex, v_uv);\n"
"    frag = vec4(tex.rgb * v_tint.rgb * light, tex.a * v_tint.a);\n"
"}\n";

static const char *const ZAN_GAME_3D_FS_INST_COLOR =
ZAN_GAME_3D_LIGHT_UNIFORMS
"uniform sampler2D uTex;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"in vec4 v_color;\n"
"in vec4 v_tint;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_LIGHT_BODY
"    vec4 tex = texture(uTex, v_uv);\n"
"    vec3 rgb = tex.rgb * v_tint.rgb * v_color.rgb * light;\n"
"    frag = vec4(rgb, tex.a * v_tint.a * v_color.a);\n"
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
    if (g_game_gl) {
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                if (g_game_progs[r][c].program)
                    g_game_gl->DeleteProgram(g_game_progs[r][c].program);
        if (g_game_instance_vbo)
            g_game_gl->DeleteBuffers(1, &g_game_instance_vbo);
    }
    if (g_game_meshes) {
        memset(g_game_meshes, 0, (size_t)g_game_mesh_cap * sizeof(zan_game_mesh));
    }
    memset(g_game_depth, 0, sizeof(g_game_depth));
    memset(g_game_progs, 0, sizeof(g_game_progs));
    g_game_instance_vbo = 0;
    g_game_batch_surface = -1;
    g_game_epoch = 0;
    g_game_gl = NULL;
}

static void zan_game_gpu_cleanup(int32_t surface_id) {
    if (surface_id >= 0) {
        if (surface_id < ZAN_GAME_SURFACE_CAP && g_game_gl)
            zan_game_drop_depth(&g_game_depth[surface_id]);
        return;
    }
    if (surface_id != -1) return;
    if (g_game_gl) {
        if (g_game_meshes) {
            for (int i = 0; i < g_game_mesh_cap; ++i)
                zan_game_drop_mesh(&g_game_meshes[i]);
        }
        for (int i = 0; i < ZAN_GAME_SURFACE_CAP; ++i)
            zan_game_drop_depth(&g_game_depth[i]);
    }
    zan_game_forget_gpu();
}

static int zan_game_gpu_prepare(const zan_gpu_frame *frame) {
    if (!frame->api || !frame->framebuffer || frame->width <= 0 ||
        frame->height <= 0) return 0;
    if (g_game_gl && g_game_epoch != frame->epoch) {
        zan_game_forget_gpu();
    }
    g_game_gl = frame->api;
    g_game_epoch = frame->epoch;
    if (!g_game_cleanup_registered) {
        if (!zan_gui_gpu_register_cleanup(zan_game_gpu_cleanup)) return 0;
        g_game_cleanup_registered = 1;
    }
    for (int i = 0; i < ZAN_GAME_GL_ERROR_LIMIT; ++i)
        if (g_game_gl->GetError() == ZGL_NO_ERROR) return 1;
    return 0;
}

/* 惰性取用着色器程序变体。属性位置为显式 layout 常量，创建时校验链接器
 * 确实落在这些位置（VAO 配置按常量位置写死，错位即整体失败，不带病运行）。 */
static zan_game_program *zan_game_program_get(int colored, int instanced) {
    zan_game_program *prog =
        &g_game_progs[colored ? 1 : 0][instanced ? 1 : 0];
    if (prog->program) return prog;
    const char *vs = instanced
        ? (colored ? ZAN_GAME_3D_VS_INST_COLOR : ZAN_GAME_3D_VS_INST)
        : (colored ? ZAN_GAME_3D_VS_COLOR : ZAN_GAME_3D_VS_PLAIN);
    const char *fs = instanced
        ? (colored ? ZAN_GAME_3D_FS_INST_COLOR : ZAN_GAME_3D_FS_INST)
        : (colored ? ZAN_GAME_3D_FS_COLOR : ZAN_GAME_3D_FS_PLAIN);
    /* 方言头 + 本体拼成完整源（缓冲按最长变体留足，截断只会导致
     * 编译失败整体关停，不会带病运行）。 */
    char vs_src[1600];
    char fs_src[1600];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, vs);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, fs);
    zgl_uint program = zan_gui_gpu_program(vs_src, fs_src);
    if (!program) return NULL;
    zgl_int tex = g_game_gl->GetUniformLocation(program, "uTex");
    zgl_int pos = g_game_gl->GetAttribLocation(program, "a_pos");
    zgl_int normal = g_game_gl->GetAttribLocation(program, "a_normal");
    zgl_int uv = g_game_gl->GetAttribLocation(program, "a_uv");
    zgl_int mvp = instanced ? 0
        : g_game_gl->GetUniformLocation(program, "uMVP");
    zgl_int color = instanced ? 0
        : g_game_gl->GetUniformLocation(program, "uColor");
    zgl_int light_dir = g_game_gl->GetUniformLocation(program, "uLightDir[0]");
    zgl_int light_color =
        g_game_gl->GetUniformLocation(program, "uLightColor[0]");
    zgl_int light_count = g_game_gl->GetUniformLocation(program, "uLightCount");
    zgl_int ambient = g_game_gl->GetUniformLocation(program, "uAmbient");
    int ok = tex >= 0 && pos == ZAN_GAME_LOC_POS &&
        normal == ZAN_GAME_LOC_NORMAL && uv == ZAN_GAME_LOC_UV &&
        mvp >= 0 && color >= 0 &&
        light_dir >= 0 && light_color >= 0 &&
        light_count >= 0 && ambient >= 0 &&
        g_game_gl->GetError() == ZGL_NO_ERROR;
    if (ok && colored) {
        ok = g_game_gl->GetAttribLocation(program, "a_color") ==
            ZAN_GAME_LOC_COLOR;
    }
    if (ok && instanced) {
        ok = g_game_gl->GetAttribLocation(program, "i_mvp") ==
                ZAN_GAME_LOC_IMVP &&
            g_game_gl->GetAttribLocation(program, "i_tint") ==
                ZAN_GAME_LOC_ITINT;
    }
    if (!ok) {
        g_game_gl->DeleteProgram(program);
        return NULL;
    }
    prog->program = program;
    prog->u_mvp = mvp;
    prog->u_tex = tex;
    prog->u_color = color;
    prog->u_light_dir = light_dir;
    prog->u_light_color = light_color;
    prog->u_light_count = light_count;
    prog->u_ambient = ambient;
    prog->light_version = 0;
    return prog;
}

/* 光照 uniform 上传：程序间 uniform 状态互不共享，每个程序按全局版本
 * 号判重；版本只在 set_lights3d 递增，常态绘制零额外调用。 */
static void zan_game_upload_lights(zan_game_program *prog) {
    if (prog->light_version == g_game_light_version) return;
    const zan_gl_api *gl = g_game_gl;
    gl->Uniform3fv(prog->u_light_dir, ZAN_GAME_MAX_LIGHTS,
        &g_game_light_dir[0][0]);
    gl->Uniform3fv(prog->u_light_color, ZAN_GAME_MAX_LIGHTS,
        &g_game_light_color[0][0]);
    gl->Uniform1i(prog->u_light_count, g_game_light_count);
    gl->Uniform3fv(prog->u_ambient, 1, g_game_ambient);
    prog->light_version = g_game_light_version;
}

static zan_game_mesh *zan_game_mesh_alloc(void) {
    if (!g_game_meshes) {
        g_game_meshes = (zan_game_mesh *)calloc(ZAN_GAME_MESH_INIT_CAP, sizeof(zan_game_mesh));
        if (!g_game_meshes) return NULL;
        g_game_mesh_cap = ZAN_GAME_MESH_INIT_CAP;
    }
    for (int i = 0; i < g_game_mesh_cap; ++i) {
        if (!g_game_meshes[i].id) {
            return &g_game_meshes[i];
        }
    }
    if (g_game_mesh_cap < ZAN_GAME_MESH_MAX_CAP) {
        int new_cap = g_game_mesh_cap * 2;
        if (new_cap > ZAN_GAME_MESH_MAX_CAP) new_cap = ZAN_GAME_MESH_MAX_CAP;
        zan_game_mesh *new_arr = (zan_game_mesh *)realloc(g_game_meshes, (size_t)new_cap * sizeof(zan_game_mesh));
        if (new_arr) {
            memset(new_arr + g_game_mesh_cap, 0, (size_t)(new_cap - g_game_mesh_cap) * sizeof(zan_game_mesh));
            int slot = g_game_mesh_cap;
            g_game_meshes = new_arr;
            g_game_mesh_cap = new_cap;
            return &g_game_meshes[slot];
        }
    }
    return NULL;
}

static zan_game_mesh *zan_game_find_mesh(int32_t id, uint64_t epoch) {
    if (id <= 0 || !g_game_meshes || g_game_mesh_cap <= 0) return NULL;
    int fast_slot = (uint32_t)(id - 1) % (uint32_t)g_game_mesh_cap;
    if (g_game_meshes[fast_slot].id == id && g_game_meshes[fast_slot].epoch == epoch) {
        return &g_game_meshes[fast_slot];
    }
    for (int i = 0; i < g_game_mesh_cap; ++i)
        if (g_game_meshes[i].id == id && g_game_meshes[i].epoch == epoch)
            return &g_game_meshes[i];
    return NULL;
}

ZAN_GAME_API int32_t zan_game_mesh_create(int32_t surface_id, const float *verts,
    int32_t count, const unsigned short *indices, int32_t index_count) {
    return zan_game_mesh_create_ex(surface_id, verts, count, indices,
                                   index_count, 0);
}

/* flags bit0 = colored：顶点 12 float（pos3+normal3+uv2+color4），
 * 否则 8 float。返回网格 id，0 = 失败/后端不支持。 */
ZAN_GAME_API int32_t zan_game_mesh_create_ex(int32_t surface_id,
    const float *verts, int32_t count, const unsigned short *indices,
    int32_t index_count, int32_t flags) {
    int colored = (flags & 1) != 0;
    int vertex_floats = colored ? ZAN_GAME_COLORED_VERTEX_FLOATS
                                : ZAN_GAME_VERTEX_FLOATS;
    if (!verts || !indices || count <= 0 || count > ZAN_GAME_VERTEX_CAP ||
        index_count <= 0 || g_game_next_mesh_id > INT32_MAX ||
        (size_t)index_count > (size_t)PTRDIFF_MAX / sizeof(*indices)) return 0;
    for (int32_t i = 0; i < index_count; ++i)
        if ((uint32_t)indices[i] >= (uint32_t)count) return 0;

    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_mesh *mesh = zan_game_mesh_alloc();
    if (!mesh || !zan_game_program_get(colored, 0)) goto done;

    const zan_gl_api *gl = frame.api;
    gl->GenVertexArrays(1, &mesh->vao);
    if (!mesh->vao) goto failed;
    gl->BindVertexArray(mesh->vao);
    gl->GenBuffers(1, &mesh->vbo);
    if (!mesh->vbo) goto failed;
    gl->BindBuffer(ZGL_ARRAY_BUFFER, mesh->vbo);
    gl->BufferData(ZGL_ARRAY_BUFFER,
        (zgl_sizeiptr)((size_t)count * vertex_floats * sizeof(*verts)),
        verts, ZGL_STATIC_DRAW);
    gl->GenBuffers(1, &mesh->ebo);
    if (!mesh->ebo) goto failed;
    gl->BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    gl->BufferData(ZGL_ELEMENT_ARRAY_BUFFER,
        (zgl_sizeiptr)((size_t)index_count * sizeof(*indices)), indices,
        ZGL_STATIC_DRAW);
    const int sizes[] = { 3, 3, 2 };
    const size_t offsets[] = { 0, 3 * sizeof(float), 6 * sizeof(float) };
    const int locs[] = {
        ZAN_GAME_LOC_POS, ZAN_GAME_LOC_NORMAL, ZAN_GAME_LOC_UV
    };
    for (int i = 0; i < 3; ++i) {
        gl->EnableVertexAttribArray((zgl_uint)locs[i]);
        gl->VertexAttribPointer((zgl_uint)locs[i], sizes[i], ZGL_FLOAT,
            ZGL_FALSE, vertex_floats * (zgl_sizei)sizeof(float),
            (const void *)offsets[i]);
    }
    if (colored) {
        gl->EnableVertexAttribArray(ZAN_GAME_LOC_COLOR);
        gl->VertexAttribPointer(ZAN_GAME_LOC_COLOR, 4, ZGL_FLOAT, ZGL_FALSE,
            vertex_floats * (zgl_sizei)sizeof(float),
            (const void *)(8 * sizeof(float)));
    }
    if (gl->GetError() != ZGL_NO_ERROR) goto failed;
    mesh->index_count = index_count;
    mesh->colored = colored;
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
    if (!mesh) goto done;
    zan_game_program *prog = zan_game_program_get(mesh->colored, 0);
    if (!prog) goto done;
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
    gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    gl->Disable(ZGL_BLEND);
    gl->UseProgram(prog->program);
    zan_game_upload_lights(prog);
    gl->UniformMatrix4fv(prog->u_mvp, 1, ZGL_FALSE, mvp);
    uint32_t argb = (uint32_t)color;
    gl->Uniform4f(prog->u_color,
        (float)((argb >> 16) & 255) / 255.0f,
        (float)((argb >> 8) & 255) / 255.0f,
        (float)(argb & 255) / 255.0f,
        (float)((argb >> 24) & 255) / 255.0f);
    gl->ActiveTexture(ZGL_TEXTURE0);
    gl->BindTexture(ZGL_TEXTURE_2D, tex);
    gl->Uniform1i(prog->u_tex, 0);
    gl->BindVertexArray(mesh->vao);
    gl->DrawElements(ZGL_TRIANGLES, mesh->index_count, ZGL_UNSIGNED_SHORT, NULL);
    gl->BindVertexArray(0);
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}

/* ---- 批量实例绘制 ----------------------------------------------------
 * begin 一次（清一次深度、绑定 FBO/深度/scissor、关 blend）→
 * 每网格一组 batch 提交（一次 DrawElementsInstanced，实例记录
 * mvp 16 float 列主序 + tint rgba 4 float）→ end 一次（恢复状态，
 * 整批恰好一次 Finish）。批内实例与跨网格组之间都做深度解析，
 * 这是与 draw3d（每次清深度、调用序遮挡）的本质差别。
 * 批活跃期间不要混用 draw3d/mesh_create。 */

ZAN_GAME_API int32_t zan_game_draw3d_batch_begin(int32_t surface_id) {
    if (surface_id < 0 || surface_id >= ZAN_GAME_SURFACE_CAP ||
        g_game_batch_surface != -1) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    if (!zan_game_gpu_prepare(&frame)) {
        zan_gui_gpu_end(surface_id);
        return 0;
    }
    int clip_x, clip_y, clip_w, clip_h;
    zan_game_clip(&frame, &clip_x, &clip_y, &clip_w, &clip_h);
    const zan_gl_api *gl = frame.api;
    if (!g_game_instance_vbo) {
        gl->GenBuffers(1, &g_game_instance_vbo);
        if (!g_game_instance_vbo) {
            zan_gui_gpu_end(surface_id);
            return 0;
        }
        gl->BindBuffer(ZGL_ARRAY_BUFFER, g_game_instance_vbo);
        gl->BufferData(ZGL_ARRAY_BUFFER,
            (zgl_sizeiptr)((size_t)ZAN_GAME_INSTANCE_CAP *
                ZAN_GAME_INSTANCE_FLOATS * sizeof(float)),
            NULL, ZGL_STREAM_DRAW);
        gl->BindBuffer(ZGL_ARRAY_BUFFER, 0);
    }
    if (gl->GetError() != ZGL_NO_ERROR) {
        zan_gui_gpu_end(surface_id);
        return 0;
    }
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    if (!zan_game_depth_ready(surface_id, &frame)) {
        zan_gui_gpu_end(surface_id);
        return 0;
    }
    gl->Viewport(0, 0, frame.width, frame.height);
    gl->Enable(ZGL_SCISSOR_TEST);
    gl->Scissor(clip_x, clip_y, clip_w, clip_h);
    gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    gl->Disable(ZGL_BLEND);
    g_game_batch_frame = frame;
    g_game_batch_clip_empty = !clip_w || !clip_h;
    g_game_batch_surface = surface_id;
    g_game_batch_prog = 0;
    g_game_batch_tex = 0;
    return 1;
}

/* 给网格 VAO 配实例属性（每网格一次，指向共享实例 VBO，divisor=1）。
 * mat4 属性占 location 4..7 连续四槽 + tint 在 8。 */
static int zan_game_mesh_inst_ready(zan_game_mesh *mesh, const zan_gl_api *gl) {
    if (mesh->inst_ready) return 1;
    gl->BindVertexArray(mesh->vao);
    gl->BindBuffer(ZGL_ARRAY_BUFFER, g_game_instance_vbo);
    const zgl_uint locs[] = {
        ZAN_GAME_LOC_IMVP, ZAN_GAME_LOC_IMVP + 1,
        ZAN_GAME_LOC_IMVP + 2, ZAN_GAME_LOC_IMVP + 3,
        ZAN_GAME_LOC_ITINT
    };
    for (int i = 0; i < 5; ++i)
        gl->EnableVertexAttribArray(locs[i]);
    for (int i = 0; i < 4; ++i) {
        gl->VertexAttribPointer(locs[i], 4, ZGL_FLOAT, ZGL_FALSE,
            ZAN_GAME_INSTANCE_FLOATS * (zgl_sizei)sizeof(float),
            (const void *)(size_t)(i * 4 * sizeof(float)));
        gl->VertexAttribDivisor(locs[i], 1);
    }
    gl->VertexAttribPointer(ZAN_GAME_LOC_ITINT, 4, ZGL_FLOAT, ZGL_FALSE,
        ZAN_GAME_INSTANCE_FLOATS * (zgl_sizei)sizeof(float),
        (const void *)(16 * sizeof(float)));
    gl->VertexAttribDivisor(ZAN_GAME_LOC_ITINT, 1);
    gl->BindBuffer(ZGL_ARRAY_BUFFER, 0);
    gl->BindVertexArray(0);
    if (gl->GetError() != ZGL_NO_ERROR) return 0;
    mesh->inst_ready = 1;
    return 1;
}

ZAN_GAME_API int32_t zan_game_draw3d_batch(int32_t surface_id, int32_t mesh_id,
    const char *texture, const float *instances, int32_t count) {
    if (surface_id < 0 || surface_id != g_game_batch_surface || !instances ||
        count <= 0 || count > ZAN_GAME_INSTANCE_CAP || mesh_id <= 0) return 0;
    zan_gpu_frame *frame = &g_game_batch_frame;
    if (!zan_game_gpu_prepare(frame)) return 0;
    zan_game_mesh *mesh = zan_game_find_mesh(mesh_id, frame->epoch);
    if (!mesh) return 0;
    zan_game_program *prog = zan_game_program_get(mesh->colored, 1);
    if (!prog || !g_game_instance_vbo) return 0;
    if (g_game_batch_clip_empty) return 1;

    const zan_bitmap *bitmap = texture && texture[0] ? zan_image_get(texture) : NULL;
    if (!zan_game_bitmap_valid(bitmap)) bitmap = &g_game_white;
    zgl_uint tex = zan_gui_gpu_texture(bitmap);
    if (!tex && bitmap != &g_game_white) tex = zan_gui_gpu_texture(&g_game_white);
    if (!tex) return 0;

    const zan_gl_api *gl = frame->api;
    gl->BindBuffer(ZGL_ARRAY_BUFFER, g_game_instance_vbo);
    gl->BufferSubData(ZGL_ARRAY_BUFFER, 0,
        (zgl_sizeiptr)((size_t)count * ZAN_GAME_INSTANCE_FLOATS *
            sizeof(float)), instances);
    gl->BindBuffer(ZGL_ARRAY_BUFFER, 0);
    if (!zan_game_mesh_inst_ready(mesh, gl)) return 0;
    if (prog->program != g_game_batch_prog ||
        prog->light_version != g_game_light_version) {
        gl->UseProgram(prog->program);
        gl->Uniform1i(prog->u_tex, 0);
        zan_game_upload_lights(prog);
        g_game_batch_prog = prog->program;
    }
    if (tex != g_game_batch_tex) {
        gl->ActiveTexture(ZGL_TEXTURE0);
        gl->BindTexture(ZGL_TEXTURE_2D, tex);
        g_game_batch_tex = tex;
    }
    gl->BindVertexArray(mesh->vao);
    gl->DrawElementsInstanced(ZGL_TRIANGLES, mesh->index_count,
        ZGL_UNSIGNED_SHORT, NULL, count);
    gl->BindVertexArray(0);
    return gl->GetError() == ZGL_NO_ERROR;
}

ZAN_GAME_API int32_t zan_game_draw3d_batch_end(int32_t surface_id) {
    if (surface_id < 0 || surface_id != g_game_batch_surface) return 0;
    if (g_game_gl) g_game_gl->Finish();
    g_game_batch_surface = -1;
    g_game_batch_clip_empty = 0;
    g_game_batch_prog = 0;
    g_game_batch_tex = 0;
    zan_gui_gpu_end(surface_id);
    return 1;
}

ZAN_GAME_API int32_t zan_game_set_lights3d(int32_t count, const float *dirs,
    const float *colors, const float *ambient) {
    if (count < 0 || count > ZAN_GAME_MAX_LIGHTS) return 0;
    if (count > 0 && (!dirs || !colors)) return 0;
    if (count > 0) {
        memcpy(g_game_light_dir, dirs,
            (size_t)count * 3 * sizeof(float));
        memcpy(g_game_light_color, colors,
            (size_t)count * 3 * sizeof(float));
    }
    g_game_light_count = count;
    if (ambient) memcpy(g_game_ambient, ambient, 3 * sizeof(float));
    g_game_light_version++;
    return 1;
}
