#include "zan_game.h"
#include "../gui/zan_gui_graphics.h"
#include "../image/zan_image.h"

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
#define ZAN_GAME_SKINNED_VERTEX_FLOATS 16
#define ZAN_GAME_VERTEX_CAP 65536
#define ZAN_GAME_GL_ERROR_LIMIT 16
#define ZAN_GAME_MAX_BONES 64
#define ZAN_GAME_SHADOW_MIN_SIZE 256
#define ZAN_GAME_SHADOW_MAX_SIZE 4096
#ifndef ZGL_DEPTH_COMPONENT24
#define ZGL_DEPTH_COMPONENT24 0x81A6
#endif
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
    int skinned;      /* 顶点带 a_joints + a_weights（16 float/顶点） */
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
    zgl_int u_mvp, u_model, u_light_vp, u_tex, u_shadow_map, u_shadow_on, u_color;
    zgl_int u_normal_map, u_normal_on, u_normal_scale;
    zgl_int u_orm_map, u_orm_on, u_ao_strength, u_alpha_cutoff;
    zgl_int u_emissive_map, u_emissive_on, u_uv_transform, u_rim_light, u_toon;
    zgl_int u_dissolve, u_dissolve_edge, u_clip_plane, u_clip_on;
    zgl_int u_triplanar, u_height_fog_params, u_height_fog_color, u_hit_flash;
    zgl_int u_holo_scan_params, u_holo_scan_color;
    zgl_int u_top_cover_params, u_top_cover_color;
    zgl_int u_wind_params, u_dist_fade;
    zgl_int u_world_grid_params, u_world_grid_color;
    zgl_int u_caustics_params, u_caustics_color;
    zgl_int u_ice_frost_params, u_ice_frost_color;
    zgl_int u_lava_crust_params, u_lava_crust_color;
    zgl_int u_iridescence_params;
    zgl_int u_sonar_origin_radius, u_sonar_style, u_sonar_color;
    zgl_int u_force_field_params, u_force_field_color;
    zgl_int u_rain_ripples_params, u_rain_ripples_color;
    zgl_int u_xray_params, u_xray_color;
    zgl_int u_portal_vortex_params, u_portal_vortex_color;
    zgl_int u_blueprint_wire_params, u_blueprint_wire_color;
    zgl_int u_heat_haze_params, u_heat_haze_color;
    zgl_int u_pixel_dither_params;
    zgl_int u_crosshatch_params, u_crosshatch_color;
    zgl_int u_aniso_brushed_params, u_aniso_brushed_color;
    zgl_int u_light_dir, u_light_color, u_light_count, u_ambient;
    zgl_int u_cam_pos, u_cam_valid, u_material, u_emissive;
    zgl_int u_point_pos, u_point_color, u_point_count;
    zgl_int u_spot_pos, u_spot_dir, u_spot_color, u_spot_count;
    zgl_int u_hemi_sky, u_hemi_ground, u_hemi_on;
    zgl_int u_fog_color, u_fog_params;
    zgl_int u_bones;
    uint32_t light_version;
} zan_game_program;

typedef struct {
    zgl_uint program;
    zgl_int u_light_vp;
    zgl_int u_model;
    zgl_int u_bones;
    zgl_int u_wind_params;
} zan_game_shadow_program;

#define ZAN_GAME_LOC_POS 0
#define ZAN_GAME_LOC_NORMAL 1
#define ZAN_GAME_LOC_UV 2
#define ZAN_GAME_LOC_COLOR 3
#define ZAN_GAME_LOC_IMVP 4   /* mat4 占用 4..7 */
#define ZAN_GAME_LOC_ITINT 8
#define ZAN_GAME_LOC_JOINTS 9
#define ZAN_GAME_LOC_WEIGHTS 10

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
static float g_game_cam_pos[3] = { 0.0f, 0.0f, 0.0f };
static int g_game_cam_valid = 0;
static float g_game_material[2] = { 0.0f, 1.0f }; /* metallic, roughness */
static float g_game_emissive[3] = { 0.0f, 0.0f, 0.0f };
static float g_game_point_pos[ZAN_GAME_MAX_LIGHTS][4];
static float g_game_point_color[ZAN_GAME_MAX_LIGHTS][3];
static int g_game_point_count = 0;
static float g_game_spot_pos[ZAN_GAME_MAX_LIGHTS][4];
static float g_game_spot_dir[ZAN_GAME_MAX_LIGHTS][4];
static float g_game_spot_color[ZAN_GAME_MAX_LIGHTS][4];
static int g_game_spot_count = 0;
static float g_game_hemi_sky[3] = { 0.35f, 0.50f, 0.72f };
static float g_game_hemi_ground[3] = { 0.16f, 0.14f, 0.12f };
static int g_game_hemi_on = 0;
static float g_game_fog_color[3] = { 0.0f, 0.0f, 0.0f };
static float g_game_fog_params[3] = { 0.0f, 0.0f, 0.0f }; /* enabled, start, invRange */
static char g_game_normal_key[256] = { 0 };
static float g_game_normal_scale = 0.0f;
static char g_game_orm_key[256] = { 0 };
static float g_game_ao_strength = 1.0f;
static float g_game_alpha_cutoff = 0.0f;
static char g_game_emissive_key[256] = { 0 };
static float g_game_uv_transform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
static float g_game_rim_light[4] = { 0.0f, 0.0f, 0.0f, 3.0f };
static float g_game_toon[2] = { 0.0f, 0.35f }; /* bands, specular_step */
static float g_game_dissolve[2] = { 0.0f, 0.08f }; /* threshold, edge_width */
static float g_game_dissolve_edge[3] = { 1.0f, 0.4f, 0.05f };
static float g_game_clip_plane[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
static int g_game_clip_on = 0;
static float g_game_triplanar[3] = { 0.0f, 1.0f, 4.0f }; /* enabled, scale, sharpness */
static float g_game_height_fog_params[4] = { 0.0f, 1.0f, 0.8f, 0.0f }; /* base_y, falloff, max_density, enabled */
static float g_game_height_fog_color[3] = { 0.6f, 0.7f, 0.85f };
static float g_game_hit_flash[4] = { 1.0f, 1.0f, 1.0f, 0.0f }; /* r, g, b, amount */
static float g_game_holo_scan_params[4] = { 0.0f, 12.0f, 0.0f, 0.0f }; /* enabled, freq, phase, strength */
static float g_game_holo_scan_color[3] = { 0.0f, 0.9f, 1.0f };
static float g_game_top_cover_params[4] = { 0.0f, 0.5f, 0.25f, 0.0f }; /* enabled, threshold, sharpness, amount */
static float g_game_top_cover_color[3] = { 0.95f, 0.97f, 1.0f };
static float g_game_wind_params[4] = { 0.0f, 1.5f, 0.0f, 1.0f }; /* amplitude, freq, phase, height_scale */
static float g_game_dist_fade[3] = { 0.0f, 0.5f, 2.0f }; /* enabled, start_dist, end_dist */
static float g_game_world_grid_params[4] = { 0.0f, 1.0f, 0.05f, 0.0f }; /* enabled, cell_size, line_width, strength */
static float g_game_world_grid_color[3] = { 0.0f, 0.9f, 1.0f };
static float g_game_caustics_params[4] = { 0.0f, 1.0f, 0.0f, 0.0f }; /* enabled, scale, phase, strength */
static float g_game_caustics_color[3] = { 0.4f, 0.9f, 1.0f };
static float g_game_ice_frost_params[4] = { 0.0f, 2.0f, 0.0f, 2.5f }; /* enabled, scale, glint_strength, fresnel_power */
static float g_game_ice_frost_color[3] = { 0.65f, 0.92f, 1.0f };
static float g_game_lava_crust_params[4] = { 0.0f, 1.5f, 0.0f, 0.18f }; /* enabled, scale, phase, crack_width */
static float g_game_lava_crust_color[4] = { 1.0f, 0.35f, 0.04f, 0.0f }; /* r, g, b, strength */
static float g_game_iridescence_params[4] = { 0.0f, 2.0f, 0.0f, 0.0f }; /* enabled, film_thickness, phase_shift, strength */
static float g_game_sonar_origin_radius[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; /* ox, oy, oz, radius */
static float g_game_sonar_style[3] = { 0.0f, 0.6f, 0.0f }; /* enabled, ring_width, strength */
static float g_game_sonar_color[3] = { 0.1f, 0.9f, 1.0f };
static float g_game_force_field_params[4] = { 0.0f, 2.0f, 0.0f, 0.10f }; /* enabled, scale, phase, edge_width */
static float g_game_force_field_color[4] = { 0.1f, 0.75f, 1.0f, 0.0f }; /* r, g, b, strength */
static float g_game_rain_ripples_params[4] = { 0.0f, 2.0f, 0.0f, 0.0f }; /* enabled, scale, phase, strength */
static float g_game_rain_ripples_color[3] = { 0.75f, 0.88f, 1.0f };
static float g_game_xray_params[3] = { 0.0f, 2.5f, 0.0f }; /* enabled, power, strength */
static float g_game_xray_color[3] = { 0.2f, 0.85f, 1.0f };
static float g_game_portal_vortex_params[4] = { 0.0f, 3.0f, 0.0f, 0.0f }; /* enabled, swirl_turns, phase, strength */
static float g_game_portal_vortex_color[3] = { 0.55f, 0.25f, 1.0f };
static float g_game_blueprint_wire_params[4] = { 0.0f, 2.0f, 0.06f, 0.0f }; /* enabled, density, line_width, strength */
static float g_game_blueprint_wire_color[3] = { 0.15f, 0.78f, 1.0f };
static float g_game_heat_haze_params[4] = { 0.0f, 4.0f, 0.0f, 0.0f }; /* enabled, freq, phase, strength */
static float g_game_heat_haze_color[3] = { 1.0f, 0.55f, 0.18f };
static float g_game_pixel_dither_params[3] = { 0.0f, 1.0f, 1.0f }; /* enabled, opacity, pixel_scale */
static float g_game_crosshatch_params[3] = { 0.0f, 8.0f, 0.0f }; /* enabled, density, ink_strength */
static float g_game_crosshatch_color[3] = { 0.05f, 0.05f, 0.08f };
static float g_game_aniso_brushed_params[4] = { 0.0f, 12.0f, 16.0f, 0.0f }; /* enabled, grain_freq, anisotropy, strength */
static float g_game_aniso_brushed_color[3] = { 0.85f, 0.90f, 1.0f };
static float g_game_color_grade[2] = { 1.0f, 1.0f }; /* saturation, contrast */
static float g_game_color_tint[3] = { 1.0f, 1.0f, 1.0f };
static float g_game_post_lens[2] = { 0.0f, 0.0f }; /* fxaa, chromatic */
static float g_game_post_film[3] = { 0.0f, 0.0f, 0.0f }; /* sharpen, grain, seed */
static float g_game_post_cinema[4] = { 0.0f, 0.0f, 16.0f, 0.0f }; /* letterbox, amp, freq, phase */
static float g_game_post_radial_blur[3] = { 0.0f, 0.5f, 0.5f }; /* strength, cx, cy */
static float g_game_post_pixelate[2] = { 0.0f, 0.0f }; /* pixel_size, posterize_levels */
static float g_game_post_duotone_dark[4] = { 0.0f, 0.05f, 0.0f, 0.0f }; /* dark_r, g, b, strength */
static float g_game_post_duotone_light[3] = { 0.2f, 1.0f, 0.35f }; /* light_r, g, b */
static float g_game_post_crt[4] = { 0.0f, 64.0f, 0.0f, 1.0f }; /* intensity, count, curvature, glow_boost */
static float g_game_post_outline_params[2] = { 0.0f, 0.2f }; /* strength, threshold */
static float g_game_post_outline_color[3] = { 0.0f, 0.0f, 0.0f };
static float g_game_post_thermal[3] = { 0.0f, 1.0f, 0.0f }; /* strength, sensitivity, cold_shift */
static float g_game_post_flash_params[2] = { 0.0f, 0.0f }; /* edge_strength, flash_strength */
static float g_game_post_flash_color[3] = { 1.0f, 0.05f, 0.05f };
static float g_game_post_night_vision[4] = { 0.0f, 3.0f, 0.06f, 0.0f }; /* strength, gain, noise_amount, seed */
static float g_game_post_halftone[3] = { 0.0f, 6.0f, 1.0f }; /* strength, dot_scale, contrast */
static float g_game_post_glitch[3] = { 0.0f, 24.0f, 0.0f }; /* intensity, slice_count, seed */
static float g_game_post_tilt_shift[3] = { 0.0f, 0.5f, 0.15f }; /* strength, focus_y, focus_band */
static float g_game_post_frost_params[2] = { 0.0f, 0.45f }; /* intensity, radius */
static float g_game_post_frost_color[3] = { 0.55f, 0.88f, 1.0f };
static float g_game_post_sepia[3] = { 0.0f, 0.0f, 0.0f }; /* strength, scratch_amount, seed */
static float g_game_post_streak_params[2] = { 0.0f, 0.75f }; /* strength, threshold */
static float g_game_post_streak_color[3] = { 0.25f, 0.78f, 1.0f };
static float g_game_post_split_params[2] = { 0.0f, 0.0f }; /* strength, balance */
static float g_game_post_split_shadow[3] = { 0.05f, 0.35f, 0.95f };
static float g_game_post_split_high[3] = { 1.0f, 0.58f, 0.15f };
static float g_game_post_oil_paint[2] = { 0.0f, 2.0f }; /* strength, radius */
static float g_game_post_scope_blur[2] = { 0.0f, 0.45f }; /* strength, clear_radius */
static float g_game_post_daltonize[2] = { 0.0f, 0.0f }; /* mode (0..3), strength (0..1) */
static uint32_t g_game_light_version = 1;

static zan_game_mesh *g_game_meshes = NULL;
static int g_game_mesh_cap = 0;
static zan_game_depth g_game_depth[ZAN_GAME_SURFACE_CAP];
static const zan_gl_api *g_game_gl;
static uint64_t g_game_epoch;
static zan_game_program g_game_progs[2][2];
static zan_game_program g_game_skin_prog;
static zan_game_shadow_program g_game_shadow_prog;
static zan_game_shadow_program g_game_shadow_skin_prog;
static zgl_uint g_game_instance_vbo;
static int32_t g_game_batch_surface = -1;
static zan_gpu_frame g_game_batch_frame;
static int g_game_batch_clip_empty;
static zgl_uint g_game_batch_prog = 0;
static zgl_uint g_game_batch_tex = 0;
static int g_game_cleanup_registered;
static uint32_t g_game_next_mesh_id = 1;

/* Alpha/Additive 混合、硬件面剔除、深度保留、阴影贴图与 GPU 骨骼矩阵全局状态 */
static int g_game_blend_enabled = 0; /* 0=off, 1=alpha, 2=additive */
static int g_game_depth_write = 1;
static int g_game_cull_mode = 0;     /* 0=off, 1=back, 2=front */
static int g_game_preserve_depth = 0;
static zgl_uint g_game_shadow_fbo = 0;
static zgl_uint g_game_shadow_tex = 0;
static int g_game_shadow_size = 0;
static int g_game_shadow_ready = 0;
static int32_t g_game_shadow_surface = -1;
static zan_gpu_frame g_game_shadow_frame;
static float g_game_shadow_light_vp[16] = {
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
};
static const float g_game_identity_mat4[16] = {
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
};
static float g_game_bones[ZAN_GAME_MAX_BONES * 16];
static int g_game_bone_count = 0;

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
 * 网格 VAO。不翻 Y：画布 FBO 是标准 GL 底朝上布局（2D 着色器
 * ndc.y = 1 - y/H*2、回读逐行倒序、present 原样 blit、天空盒与 scissor
 * 都按此约定），clip 直接输出即可。曾在这里对 clip.y 取反，整幅 3D
 * 世界上下颠倒而 HUD 正常（macOS 上 fps-3d 可见）。 */

static const char *const ZAN_GAME_3D_VS_PLAIN =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModel;\n"
"uniform mat4 uLightVP;\n"
"uniform vec4 uUvTransform;\n"
"uniform vec4 uWindParams;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec3 v_world_pos;\n"
"out vec4 v_light_pos;\n"
"void main() {\n"
"    vec3 lp = a_pos;\n"
"    if (uWindParams.x > 0.0001) {\n"
"        float wh = max(lp.y, 0.0) * uWindParams.w;\n"
"        float ww = sin(uWindParams.z + lp.x * uWindParams.y + lp.z * uWindParams.y * 0.7) * uWindParams.x * wh;\n"
"        lp.x += ww;\n"
"        lp.z += ww * 0.35;\n"
"    }\n"
"    vec4 wp = uModel * vec4(lp, 1.0);\n"
"    v_uv = a_uv * uUvTransform.xy + uUvTransform.zw;\n"
"    v_normal = normalize(mat3(uModel) * a_normal);\n"
"    v_world_pos = wp.xyz;\n"
"    v_light_pos = uLightVP * wp;\n"
"    vec4 clip = uMVP * vec4(lp, 1.0);\n"
"    gl_Position = clip;\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_COLOR =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"layout(location = 3) in vec4 a_color;\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModel;\n"
"uniform mat4 uLightVP;\n"
"uniform vec4 uUvTransform;\n"
"uniform vec4 uWindParams;\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec3 v_world_pos;\n"
"out vec4 v_color;\n"
"out vec4 v_light_pos;\n"
"void main() {\n"
"    vec3 lp = a_pos;\n"
"    if (uWindParams.x > 0.0001) {\n"
"        float wh = max(lp.y, 0.0) * uWindParams.w;\n"
"        float ww = sin(uWindParams.z + lp.x * uWindParams.y + lp.z * uWindParams.y * 0.7) * uWindParams.x * wh;\n"
"        lp.x += ww;\n"
"        lp.z += ww * 0.35;\n"
"    }\n"
"    vec4 wp = uModel * vec4(lp, 1.0);\n"
"    v_uv = a_uv * uUvTransform.xy + uUvTransform.zw;\n"
"    v_normal = normalize(mat3(uModel) * a_normal);\n"
"    v_world_pos = wp.xyz;\n"
"    v_color = a_color;\n"
"    v_light_pos = uLightVP * wp;\n"
"    vec4 clip = uMVP * vec4(lp, 1.0);\n"
"    gl_Position = clip;\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_SKINNED =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec2 a_uv;\n"
"layout(location = 9) in vec4 a_joints;\n"
"layout(location = 10) in vec4 a_weights;\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModel;\n"
"uniform mat4 uLightVP;\n"
"uniform vec4 uUvTransform;\n"
"uniform vec4 uWindParams;\n"
"uniform mat4 uBones[64];\n"
"out vec2 v_uv;\n"
"out vec3 v_normal;\n"
"out vec3 v_world_pos;\n"
"out vec4 v_light_pos;\n"
"void main() {\n"
"    ivec4 ji = clamp(ivec4(a_joints + vec4(0.5)), ivec4(0), ivec4(63));\n"
"    float wsum = a_weights.x + a_weights.y + a_weights.z + a_weights.w;\n"
"    mat4 skin = (wsum > 0.0001)\n"
"        ? (uBones[ji.x] * a_weights.x + uBones[ji.y] * a_weights.y +\n"
"           uBones[ji.z] * a_weights.z + uBones[ji.w] * a_weights.w)\n"
"        : mat4(1.0);\n"
"    vec4 sp = skin * vec4(a_pos, 1.0);\n"
"    if (uWindParams.x > 0.0001) {\n"
"        float wh = max(sp.y, 0.0) * uWindParams.w;\n"
"        float ww = sin(uWindParams.z + sp.x * uWindParams.y + sp.z * uWindParams.y * 0.7) * uWindParams.x * wh;\n"
"        sp.x += ww;\n"
"        sp.z += ww * 0.35;\n"
"    }\n"
"    vec3 sn = normalize(mat3(skin) * a_normal);\n"
"    vec4 wp = uModel * sp;\n"
"    v_uv = a_uv * uUvTransform.xy + uUvTransform.zw;\n"
"    v_normal = normalize(mat3(uModel) * sn);\n"
"    v_world_pos = wp.xyz;\n"
"    v_light_pos = uLightVP * wp;\n"
"    vec4 clip = uMVP * sp;\n"
"    gl_Position = clip;\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_SHADOW =
"layout(location = 0) in vec3 a_pos;\n"
"uniform mat4 uLightVP;\n"
"uniform mat4 uModel;\n"
"uniform vec4 uWindParams;\n"
"void main() {\n"
"    vec3 lp = a_pos;\n"
"    if (uWindParams.x > 0.0001) {\n"
"        float wh = max(lp.y, 0.0) * uWindParams.w;\n"
"        float ww = sin(uWindParams.z + lp.x * uWindParams.y + lp.z * uWindParams.y * 0.7) * uWindParams.x * wh;\n"
"        lp.x += ww;\n"
"        lp.z += ww * 0.35;\n"
"    }\n"
"    gl_Position = uLightVP * (uModel * vec4(lp, 1.0));\n"
"}\n";

static const char *const ZAN_GAME_3D_VS_SHADOW_SKINNED =
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 9) in vec4 a_joints;\n"
"layout(location = 10) in vec4 a_weights;\n"
"uniform mat4 uLightVP;\n"
"uniform mat4 uModel;\n"
"uniform mat4 uBones[64];\n"
"void main() {\n"
"    ivec4 ji = clamp(ivec4(a_joints + vec4(0.5)), ivec4(0), ivec4(63));\n"
"    float wsum = a_weights.x + a_weights.y + a_weights.z + a_weights.w;\n"
"    mat4 skin = (wsum > 0.0001)\n"
"        ? (uBones[ji.x] * a_weights.x + uBones[ji.y] * a_weights.y +\n"
"           uBones[ji.z] * a_weights.z + uBones[ji.w] * a_weights.w)\n"
"        : mat4(1.0);\n"
"    vec4 sp = skin * vec4(a_pos, 1.0);\n"
"    gl_Position = uLightVP * (uModel * sp);\n"
"}\n";

static const char *const ZAN_GAME_3D_FS_SHADOW =
"void main() {}\n";

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
"    gl_Position = clip;\n"
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
"    gl_Position = clip;\n"
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

#define ZAN_GAME_3D_SHADOW_UNIFORMS \
"uniform sampler2D uShadowMap;\n" \
"uniform int uShadowOn;\n" \
"uniform sampler2D uNormalMap;\n" \
"uniform int uNormalOn;\n" \
"uniform float uNormalScale;\n" \
"uniform sampler2D uOrmMap;\n" \
"uniform int uOrmOn;\n" \
"uniform float uAoStrength;\n" \
"uniform float uAlphaCutoff;\n" \
"uniform sampler2D uEmissiveMap;\n" \
"uniform int uEmissiveOn;\n" \
"uniform vec4 uRimLight;\n" \
"uniform vec2 uToon;\n" \
"uniform vec2 uDissolve;\n" \
"uniform vec3 uDissolveEdge;\n" \
"uniform vec4 uClipPlane;\n" \
"uniform int uClipOn;\n" \
"uniform vec3 uTriplanar;\n" \
"uniform vec4 uHeightFogParams;\n" \
"uniform vec3 uHeightFogColor;\n" \
"uniform vec4 uHitFlash;\n" \
"uniform vec4 uHoloScanParams;\n" \
"uniform vec3 uHoloScanColor;\n" \
"uniform vec4 uTopCoverParams;\n" \
"uniform vec3 uTopCoverColor;\n" \
"uniform vec3 uDistFade;\n" \
"uniform vec4 uWorldGridParams;\n" \
"uniform vec3 uWorldGridColor;\n" \
"uniform vec4 uCausticsParams;\n" \
"uniform vec3 uCausticsColor;\n" \
"uniform vec4 uIceFrostParams;\n" \
"uniform vec3 uIceFrostColor;\n" \
"uniform vec4 uLavaCrustParams;\n" \
"uniform vec4 uLavaCrustColor;\n" \
"uniform vec4 uIridescenceParams;\n" \
"uniform vec4 uSonarOriginRadius;\n" \
"uniform vec3 uSonarStyle;\n" \
"uniform vec3 uSonarColor;\n" \
"uniform vec4 uForceFieldParams;\n" \
"uniform vec4 uForceFieldColor;\n" \
"uniform vec4 uRainRipplesParams;\n" \
"uniform vec3 uRainRipplesColor;\n" \
"uniform vec3 uXrayParams;\n" \
"uniform vec3 uXrayColor;\n" \
"uniform vec4 uPortalVortexParams;\n" \
"uniform vec3 uPortalVortexColor;\n" \
"uniform vec4 uBlueprintWireParams;\n" \
"uniform vec3 uBlueprintWireColor;\n" \
"uniform vec4 uHeatHazeParams;\n" \
"uniform vec3 uHeatHazeColor;\n" \
"uniform vec3 uPixelDitherParams;\n" \
"uniform vec3 uCrosshatchParams;\n" \
"uniform vec3 uCrosshatchColor;\n" \
"uniform vec4 uAnisoBrushedParams;\n" \
"uniform vec3 uAnisoBrushedColor;\n" \
"uniform vec3 uCamPos;\n" \
"uniform int uCamValid;\n" \
"uniform vec2 uMaterial;\n" \
"uniform vec3 uEmissive;\n" \
"uniform vec4 uPointPos[4];\n" \
"uniform vec3 uPointColor[4];\n" \
"uniform int uPointCount;\n" \
"uniform vec4 uSpotPos[4];\n" \
"uniform vec4 uSpotDir[4];\n" \
"uniform vec4 uSpotColor[4];\n" \
"uniform int uSpotCount;\n" \
"uniform vec3 uHemiSky;\n" \
"uniform vec3 uHemiGround;\n" \
"uniform int uHemiOn;\n" \
"uniform vec3 uFogColor;\n" \
"uniform vec3 uFogParams;\n" \
"in vec3 v_world_pos;\n" \
"in vec4 v_light_pos;\n"

#define ZAN_GAME_3D_SHADOW_FACTOR \
"    float shadowVis = 1.0;\n" \
"    if (uShadowOn != 0 && v_light_pos.w > 0.0001) {\n" \
"        vec3 proj = (v_light_pos.xyz / v_light_pos.w) * 0.5 + 0.5;\n" \
"        if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0 && proj.z <= 1.0) {\n" \
"            float bias = 0.003;\n" \
"            vec2 texel = vec2(1.0 / 1024.0);\n" \
"            float vis = 0.0;\n" \
"            for (int dx = -1; dx <= 1; ++dx) {\n" \
"                for (int dy = -1; dy <= 1; ++dy) {\n" \
"                    float sd = texture(uShadowMap, proj.xy + vec2(float(dx), float(dy)) * texel).r;\n" \
"                    vis += (proj.z - bias <= sd) ? 1.0 : 0.0;\n" \
"                }\n" \
"            }\n" \
"            shadowVis = vis / 9.0;\n" \
"        }\n" \
"    }\n"

#define ZAN_GAME_3D_LIGHT_SHADOW_BODY \
"    vec3 N = normalize(v_normal);\n" \
"    if (uNormalOn != 0 && uNormalScale > 0.0001) {\n" \
"        vec3 mapN = texture(uNormalMap, v_uv).xyz * 2.0 - 1.0;\n" \
"        mapN.xy *= uNormalScale;\n" \
"        vec3 dp1 = dFdx(v_world_pos);\n" \
"        vec3 dp2 = dFdy(v_world_pos);\n" \
"        vec2 duv1 = dFdx(v_uv);\n" \
"        vec2 duv2 = dFdy(v_uv);\n" \
"        float det = duv1.x * duv2.y - duv1.y * duv2.x;\n" \
"        if (abs(det) > 1e-10) {\n" \
"            float invDet = 1.0 / det;\n" \
"            vec3 T = (dp1 * duv2.y - dp2 * duv1.y) * invDet;\n" \
"            vec3 B = (dp2 * duv1.x - dp1 * duv2.x) * invDet;\n" \
"            T = T - N * dot(N, T);\n" \
"            B = B - N * dot(N, B);\n" \
"            if (dot(T, T) > 1e-10 && dot(B, B) > 1e-10) {\n" \
"                mat3 TBN = mat3(normalize(T), normalize(B), N);\n" \
"                N = normalize(TBN * normalize(mapN));\n" \
"            }\n" \
"        }\n" \
"    }\n" \
"    vec3 V = (uCamValid != 0) ? normalize(uCamPos - v_world_pos) : vec3(0.0, 0.0, 1.0);\n" \
"    float ao = 1.0;\n" \
"    float rough = clamp(uMaterial.y, 0.05, 1.0);\n" \
"    float metal = clamp(uMaterial.x, 0.0, 1.0);\n" \
"    if (uOrmOn != 0) {\n" \
"        vec3 orm = texture(uOrmMap, v_uv).rgb;\n" \
"        ao = mix(1.0, orm.r, clamp(uAoStrength, 0.0, 1.0));\n" \
"        rough = clamp(uMaterial.y * orm.g, 0.05, 1.0);\n" \
"        metal = clamp(uMaterial.x * orm.b, 0.0, 1.0);\n" \
"    }\n" \
"    float specPow = mix(128.0, 4.0, rough * rough);\n" \
"    float specStrength = (1.0 - rough) * mix(0.35, 1.0, metal);\n" \
"    vec3 light = ((uHemiOn != 0) ? mix(uHemiGround, uHemiSky, clamp(N.y * 0.5 + 0.5, 0.0, 1.0)) : uAmbient) * ao;\n" \
"    vec3 specAcc = vec3(0.0);\n" \
"    for (int i = 0; i < 4; ++i) {\n" \
"        if (i >= uLightCount) break;\n" \
"        vec3 L = normalize(uLightDir[i]);\n" \
"        float lam = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);\n" \
"        float sf = (i == 0) ? shadowVis : 1.0;\n" \
"        light += uLightColor[i] * lam * sf;\n" \
"        if (uCamValid != 0 && specStrength > 0.001) {\n" \
"            vec3 H = normalize(L + V);\n" \
"            float ndh = max(dot(N, H), 0.0);\n" \
"            specAcc += uLightColor[i] * (pow(ndh, specPow) * specStrength * sf);\n" \
"        }\n" \
"    }\n" \
"    for (int p = 0; p < 4; ++p) {\n" \
"        if (p >= uPointCount) break;\n" \
"        vec3 toL = uPointPos[p].xyz - v_world_pos;\n" \
"        float dist = length(toL);\n" \
"        float rad = uPointPos[p].w;\n" \
"        if (rad > 0.0001 && dist < rad) {\n" \
"            vec3 L = (dist > 0.0001) ? (toL / dist) : vec3(0.0, 1.0, 0.0);\n" \
"            float att = 1.0 - (dist / rad);\n" \
"            att = att * att;\n" \
"            float ndl = max(dot(N, L), 0.0);\n" \
"            light += uPointColor[p] * (ndl * att);\n" \
"            if (uCamValid != 0 && specStrength > 0.001) {\n" \
"                vec3 H = normalize(L + V);\n" \
"                float ndh = max(dot(N, H), 0.0);\n" \
"                specAcc += uPointColor[p] * (pow(ndh, specPow) * specStrength * att);\n" \
"            }\n" \
"        }\n" \
"    }\n" \
"    for (int s = 0; s < 4; ++s) {\n" \
"        if (s >= uSpotCount) break;\n" \
"        vec3 toL = uSpotPos[s].xyz - v_world_pos;\n" \
"        float dist = length(toL);\n" \
"        float rad = uSpotPos[s].w;\n" \
"        if (rad > 0.0001 && dist < rad) {\n" \
"            vec3 L = (dist > 0.0001) ? (toL / dist) : vec3(0.0, 1.0, 0.0);\n" \
"            vec3 sAxis = normalize(uSpotDir[s].xyz);\n" \
"            float cosAng = dot(-L, sAxis);\n" \
"            float cosIn = uSpotDir[s].w;\n" \
"            float cosOut = uSpotColor[s].w;\n" \
"            float cone = clamp((cosAng - cosOut) / max(cosIn - cosOut, 0.0001), 0.0, 1.0);\n" \
"            cone = cone * cone * (3.0 - 2.0 * cone);\n" \
"            if (cone > 0.0001) {\n" \
"                float att = 1.0 - (dist / rad);\n" \
"                att = att * att * cone;\n" \
"                float ndl = max(dot(N, L), 0.0);\n" \
"                light += uSpotColor[s].rgb * (ndl * att);\n" \
"                if (uCamValid != 0 && specStrength > 0.001) {\n" \
"                    vec3 H = normalize(L + V);\n" \
"                    float ndh = max(dot(N, H), 0.0);\n" \
"                    specAcc += uSpotColor[s].rgb * (pow(ndh, specPow) * specStrength * att);\n" \
"                }\n" \
"            }\n" \
"        }\n" \
"    }\n"

#define ZAN_GAME_3D_FINAL_COMPOSE(BASE_EXPR, ALPHA_EXPR) \
"    if (uClipOn != 0 && (dot(v_world_pos, uClipPlane.xyz) + uClipPlane.w) < 0.0) discard;\n" \
"    if (uDistFade.x > 0.5 && uCamValid != 0) {\n" \
"        float cd = length(v_world_pos - uCamPos);\n" \
"        float fSpan = uDistFade.z - uDistFade.y;\n" \
"        float fVis = (abs(fSpan) > 0.0001) ? clamp((cd - uDistFade.y) / fSpan, 0.0, 1.0) : 1.0;\n" \
"        if (fVis <= 0.001) discard;\n" \
"        if (fVis < 0.999) {\n" \
"            vec2 bp = mod(floor(gl_FragCoord.xy), 4.0);\n" \
"            vec2 bp0 = mod(bp, 2.0);\n" \
"            vec2 bp1 = floor(bp * 0.5);\n" \
"            float bVal = 4.0 * (2.0 * abs(bp0.x - bp0.y) + bp0.y) + (2.0 * abs(bp1.x - bp1.y) + bp1.y);\n" \
"            if (fVis * 16.0 <= bVal + 0.5) discard;\n" \
"        }\n" \
"    }\n" \
"    float outAlpha = " ALPHA_EXPR ";\n" \
"    if (uAlphaCutoff > 0.0001 && outAlpha < uAlphaCutoff) discard;\n" \
"    float dissEdgeBoost = 0.0;\n" \
"    if (uDissolve.x > 0.0001) {\n" \
"        vec2 dCell = floor(v_uv * 16.0);\n" \
"        vec2 dF = fract(v_uv * 16.0);\n" \
"        vec2 dS = dF * dF * (3.0 - 2.0 * dF);\n" \
"        float h00 = fract(sin(dot(dCell, vec2(127.1, 311.7))) * 43758.5453);\n" \
"        float h10 = fract(sin(dot(dCell + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);\n" \
"        float h01 = fract(sin(dot(dCell + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);\n" \
"        float h11 = fract(sin(dot(dCell + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);\n" \
"        float dn = mix(mix(h00, h10, dS.x), mix(h01, h11, dS.x), dS.y);\n" \
"        if (dn < uDissolve.x) discard;\n" \
"        if (uDissolve.y > 0.0001 && dn < uDissolve.x + uDissolve.y) {\n" \
"            dissEdgeBoost = (1.0 - (dn - uDissolve.x) / uDissolve.y) * 2.0;\n" \
"        }\n" \
"    }\n" \
    "    vec3 baseRgb = " BASE_EXPR ";\n" \
    "    if (uTopCoverParams.x > 0.5 && uTopCoverParams.w > 0.0001) {\n" \
    "        float upDot = normalize(v_normal).y;\n" \
    "        float tcMask = smoothstep(uTopCoverParams.y, uTopCoverParams.y + max(uTopCoverParams.z, 0.01), upDot);\n" \
    "        baseRgb = mix(baseRgb, uTopCoverColor, tcMask * clamp(uTopCoverParams.w, 0.0, 1.0));\n" \
    "    }\n" \
    "    vec3 specTint = mix(vec3(1.0), baseRgb, metal);\n" \
    "    vec3 emitRgb = (uEmissiveOn != 0) ? (uEmissive * texture(uEmissiveMap, v_uv).rgb) : uEmissive;\n" \
    "    vec3 litDiff = light;\n" \
    "    vec3 litSpec = specAcc;\n" \
    "    if (uToon.x >= 2.0) {\n" \
    "        float dl = max(litDiff.r, max(litDiff.g, litDiff.b));\n" \
    "        if (dl > 0.0001) {\n" \
    "            float q = floor(clamp(dl, 0.0, 1.0) * uToon.x + 0.5) / uToon.x;\n" \
    "            litDiff *= (q / dl);\n" \
    "        }\n" \
    "        float sl = max(litSpec.r, max(litSpec.g, litSpec.b));\n" \
    "        float spStep = (uToon.y > 0.001) ? uToon.y : 0.35;\n" \
    "        float sq = smoothstep(spStep - 0.03, spStep + 0.03, sl);\n" \
    "        litSpec = (sl > 0.0001) ? (litSpec * (sq / sl)) : vec3(0.0);\n" \
    "    }\n" \
    "    vec3 rgb = baseRgb * mix(litDiff, litDiff * (0.35 + 0.65 * rough), metal) + specTint * litSpec + emitRgb + uDissolveEdge * dissEdgeBoost;\n" \
    "    if (uWorldGridParams.x > 0.5 && uWorldGridParams.w > 0.0005) {\n" \
    "        float gCell = max(uWorldGridParams.y, 0.05);\n" \
    "        vec2 gCoord = v_world_pos.xz / gCell;\n" \
    "        vec2 gDist = abs(fract(gCoord - 0.5) - 0.5);\n" \
    "        vec2 gFw = max(fwidth(gCoord), vec2(0.001));\n" \
    "        float gHalf = clamp(uWorldGridParams.z, 0.005, 0.45);\n" \
    "        vec2 gLine = smoothstep(gHalf + gFw, max(gHalf - gFw, 0.0), gDist);\n" \
    "        float gMask = max(gLine.x, gLine.y) * clamp(uWorldGridParams.w, 0.0, 1.0);\n" \
    "        rgb = mix(rgb, uWorldGridColor, gMask);\n" \
    "    }\n" \
    "    if (uCausticsParams.x > 0.5 && uCausticsParams.w > 0.0005) {\n" \
    "        float cUp = smoothstep(0.1, 0.7, normalize(v_normal).y);\n" \
    "        if (cUp > 0.001) {\n" \
    "            vec2 cp = v_world_pos.xz * max(uCausticsParams.y, 0.1);\n" \
    "            float cPh = uCausticsParams.z;\n" \
    "            float cw1 = sin(cp.x * 3.1 + cPh) * sin(cp.y * 3.1 - cPh * 0.8);\n" \
    "            float cw2 = sin((cp.x * 0.7 + cp.y * 0.7) * 4.3 - cPh * 1.1) * sin((-cp.x * 0.7 + cp.y * 0.7) * 4.3 + cPh * 0.9);\n" \
    "            float cNet = pow(clamp(1.0 - abs(cw1 + cw2) * 0.65, 0.0, 1.0), 3.0);\n" \
    "            rgb += uCausticsColor * (cNet * cUp * clamp(uCausticsParams.w, 0.0, 2.0));\n" \
    "        }\n" \
    "    }\n" \
    "    if (uIceFrostParams.x > 0.5 && uIceFrostParams.z > 0.0005) {\n" \
    "        vec3 fN = normalize(v_normal);\n" \
    "        vec3 fV = uCamValid > 0.5 ? normalize(uCamPos - v_world_pos) : vec3(0.0, 0.0, 1.0);\n" \
    "        float fEdge = pow(clamp(1.0 - abs(dot(fN, fV)), 0.0, 1.0), max(uIceFrostParams.w, 0.5));\n" \
    "        vec3 fPos = v_world_pos * max(uIceFrostParams.y, 0.2);\n" \
    "        float fFacet = abs(sin(fPos.x * 7.3 + fPos.z * 5.1) * cos(fPos.y * 6.7 - fPos.x * 4.3));\n" \
    "        float fGlint = pow(clamp(fFacet, 0.0, 1.0), 4.0);\n" \
    "        float fStr = clamp(uIceFrostParams.z, 0.0, 2.0);\n" \
    "        rgb += uIceFrostColor * ((fEdge * 0.75 + fGlint * 0.65) * fStr);\n" \
    "    }\n" \
    "    if (uLavaCrustParams.x > 0.5 && uLavaCrustColor.w > 0.0005) {\n" \
    "        vec3 lp = v_world_pos * max(uLavaCrustParams.y, 0.2);\n" \
    "        float lField = abs(sin(lp.x * 2.7 + sin(lp.z * 2.1 + lp.y * 1.3))\n" \
    "            + cos(lp.z * 2.5 - cos(lp.x * 1.9 + lp.y * 1.7))) * 0.5;\n" \
    "        float lW = clamp(uLavaCrustParams.w, 0.02, 0.6);\n" \
    "        float lCrack = 1.0 - smoothstep(0.0, lW, lField);\n" \
    "        float lPulse = 0.72 + 0.28 * sin(uLavaCrustParams.z * 2.0 + lp.x + lp.z);\n" \
    "        rgb += uLavaCrustColor.rgb * (lCrack * lPulse * clamp(uLavaCrustColor.w, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uIridescenceParams.x > 0.5 && uIridescenceParams.w > 0.0005) {\n" \
    "        vec3 irV = (uCamValid != 0) ? normalize(uCamPos - v_world_pos) : vec3(0.0, 0.0, 1.0);\n" \
    "        float irNdv = clamp(dot(N, irV), 0.0, 1.0);\n" \
    "        float irPhase = (1.0 - irNdv) * max(uIridescenceParams.y, 0.5) * 6.2831853 + uIridescenceParams.z;\n" \
    "        vec3 irCol = vec3(0.5) + vec3(0.5) * cos(vec3(irPhase, irPhase + 2.0943951, irPhase + 4.1887902));\n" \
    "        float irFres = 0.25 + 0.75 * pow(1.0 - irNdv, 1.5);\n" \
    "        rgb += irCol * (irFres * clamp(uIridescenceParams.w, 0.0, 2.0));\n" \
    "    }\n" \
    "    if (uSonarStyle.x > 0.5 && uSonarStyle.z > 0.0005) {\n" \
    "        float sDist = length(v_world_pos - uSonarOriginRadius.xyz);\n" \
    "        float sRad = max(uSonarOriginRadius.w, 0.05);\n" \
    "        float sW = max(uSonarStyle.y, 0.05);\n" \
    "        float sBehind = sRad - sDist;\n" \
    "        float sWave = (sBehind >= 0.0 && sBehind <= sW) ? pow(1.0 - (sBehind / sW), 1.6) : 0.0;\n" \
    "        float sLead = smoothstep(sW * 0.25, 0.0, abs(sDist - sRad));\n" \
    "        float sInt = clamp(sWave + sLead * 0.55, 0.0, 1.5);\n" \
    "        rgb += uSonarColor * (sInt * clamp(uSonarStyle.z, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uForceFieldParams.x > 0.5 && uForceFieldColor.w > 0.0005) {\n" \
    "        vec2 hp = ((abs(N.y) > 0.6) ? v_world_pos.xz : vec2(v_world_pos.x + v_world_pos.z, v_world_pos.y))\n" \
    "            * max(uForceFieldParams.y, 0.25);\n" \
    "        vec2 hS = vec2(1.0, 1.7320508);\n" \
    "        vec2 hH = hS * 0.5;\n" \
    "        vec2 hA = mod(hp, hS) - hH;\n" \
    "        vec2 hB = mod(hp - hH, hS) - hH;\n" \
    "        vec2 hGv = (dot(hA, hA) < dot(hB, hB)) ? hA : hB;\n" \
    "        float hDist = max(abs(hGv.x), abs(hGv.x) * 0.5 + abs(hGv.y) * 0.8660254);\n" \
    "        float hW = clamp(uForceFieldParams.w, 0.02, 0.35);\n" \
    "        float hEdge = smoothstep(0.5 - hW, 0.5, hDist);\n" \
    "        float hPulse = 0.72 + 0.28 * sin(hp.y * 1.4 - uForceFieldParams.z * 3.0);\n" \
    "        rgb += uForceFieldColor.rgb * (hEdge * hPulse * clamp(uForceFieldColor.w, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uRainRipplesParams.x > 0.5 && uRainRipplesParams.w > 0.0005) {\n" \
    "        float rrUp = smoothstep(0.35, 0.75, N.y);\n" \
    "        if (rrUp > 0.001) {\n" \
    "            vec2 rp = v_world_pos.xz * max(uRainRipplesParams.y, 0.25);\n" \
    "            vec2 rCell = floor(rp);\n" \
    "            vec2 rF = fract(rp) - vec2(0.5);\n" \
    "            float rHash = fract(sin(dot(rCell, vec2(127.1, 311.7))) * 43758.5453);\n" \
    "            float rAge = fract(uRainRipplesParams.z + rHash);\n" \
    "            float rRad = rAge * 0.45;\n" \
    "            float rRing = smoothstep(0.08, 0.0, abs(length(rF) - rRad)) * (1.0 - rAge);\n" \
    "            rgb += uRainRipplesColor * (rRing * rrUp * clamp(uRainRipplesParams.w, 0.0, 3.0));\n" \
    "        }\n" \
    "    }\n" \
    "    if (uPortalVortexParams.x > 0.5 && uPortalVortexParams.w > 0.0005) {\n" \
    "        vec2 pv = v_uv - vec2(0.5);\n" \
    "        float pRad = length(pv);\n" \
    "        float pRing = smoothstep(0.50, 0.34, pRad) * smoothstep(0.05, 0.18, pRad);\n" \
    "        if (pRing > 0.001) {\n" \
    "            float pAng = atan(pv.y, pv.x);\n" \
    "            float pArm = 0.5 + 0.5 * sin(pAng * 4.0 + pRad * uPortalVortexParams.y * 6.2831853 - uPortalVortexParams.z * 6.2831853);\n" \
    "            rgb += uPortalVortexColor * (pRing * (0.25 + 0.75 * pArm * pArm) * clamp(uPortalVortexParams.w, 0.0, 3.0));\n" \
    "        }\n" \
    "    }\n" \
    "    if (uBlueprintWireParams.x > 0.5 && uBlueprintWireParams.w > 0.0005) {\n" \
    "        vec3 bp = abs(fract(v_world_pos * max(uBlueprintWireParams.y, 0.25) + vec3(0.5)) - vec3(0.5));\n" \
    "        float bpW = clamp(uBlueprintWireParams.z, 0.01, 0.25);\n" \
    "        vec3 bpMask = vec3(1.0) - abs(N);\n" \
    "        float bpX = bpMask.x * smoothstep(bpW, 0.0, bp.x);\n" \
    "        float bpY = bpMask.y * smoothstep(bpW, 0.0, bp.y);\n" \
    "        float bpZ = bpMask.z * smoothstep(bpW, 0.0, bp.z);\n" \
    "        float bpLine = max(bpX, max(bpY, bpZ));\n" \
    "        rgb += uBlueprintWireColor * (bpLine * clamp(uBlueprintWireParams.w, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uHeatHazeParams.x > 0.5 && uHeatHazeParams.w > 0.0005) {\n" \
    "        float hhFreq = max(uHeatHazeParams.y, 0.5);\n" \
    "        float hhW1 = sin(v_world_pos.y * hhFreq - uHeatHazeParams.z + v_world_pos.x * (hhFreq * 0.65));\n" \
    "        float hhW2 = sin(v_world_pos.y * (hhFreq * 1.6) - uHeatHazeParams.z * 1.45 - v_world_pos.z * (hhFreq * 0.75));\n" \
    "        float hhCrest = pow(clamp(0.5 + 0.25 * (hhW1 + hhW2), 0.0, 1.0), 3.0);\n" \
    "        rgb += uHeatHazeColor * (hhCrest * clamp(uHeatHazeParams.w, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uPixelDitherParams.x > 0.5) {\n" \
    "        float pdScale = max(uPixelDitherParams.z, 1.0);\n" \
    "        vec2 pdCell = mod(floor(gl_FragCoord.xy / pdScale), 4.0);\n" \
    "        vec2 pdLo = mod(pdCell, 2.0);\n" \
    "        vec2 pdHi = floor(pdCell * 0.5);\n" \
    "        float pdIdx = (pdLo.x * 2.0 + mod(pdLo.x + pdLo.y, 2.0)) * 4.0 + (pdHi.x * 2.0 + mod(pdHi.x + pdHi.y, 2.0));\n" \
    "        float pdThresh = (pdIdx + 0.5) / 16.0;\n" \
    "        if (clamp(uPixelDitherParams.y, 0.0, 1.0) < pdThresh) {\n" \
    "            discard;\n" \
    "        }\n" \
    "    }\n" \
    "    if (uCrosshatchParams.x > 0.5 && uCrosshatchParams.z > 0.0005) {\n" \
    "        float chLum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));\n" \
    "        float chStep = max(uCrosshatchParams.y, 2.0);\n" \
    "        float chD1 = abs(fract((gl_FragCoord.x + gl_FragCoord.y) / chStep) - 0.5);\n" \
    "        float chD2 = abs(fract((gl_FragCoord.x - gl_FragCoord.y) / chStep) - 0.5);\n" \
    "        float chInk = 0.0;\n" \
    "        if (chLum < 0.65) { chInk = max(chInk, smoothstep(0.18, 0.06, chD1)); }\n" \
    "        if (chLum < 0.35) { chInk = max(chInk, smoothstep(0.18, 0.06, chD2)); }\n" \
    "        rgb = mix(rgb, uCrosshatchColor, chInk * clamp(uCrosshatchParams.z, 0.0, 1.0));\n" \
    "    }\n" \
    "    if (uAnisoBrushedParams.x > 0.5 && uAnisoBrushedParams.w > 0.0005) {\n" \
    "        vec3 abRef = (abs(N.y) < 0.92) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n" \
    "        vec3 abT = normalize(cross(abRef, N));\n" \
    "        vec3 abB = cross(N, abT);\n" \
    "        float abCoord = dot(v_world_pos, abB) * max(uAnisoBrushedParams.y, 1.0);\n" \
    "        float abGroove = 0.5 + 0.5 * sin(abCoord * 6.2831853);\n" \
    "        vec3 abH = (uCamValid != 0 && uLightCount > 0) ? normalize(normalize(-uLightDir[0]) + V) : normalize(N + vec3(0.25, 0.45, 0.85));\n" \
    "        float abTdH = dot(abT, abH);\n" \
    "        float abSinTH = sqrt(max(1.0 - abTdH * abTdH, 0.0));\n" \
    "        float abSpec = pow(abSinTH, clamp(uAnisoBrushedParams.z, 2.0, 64.0));\n" \
    "        rgb += uAnisoBrushedColor * ((0.25 + 0.75 * abGroove) * (0.35 + 0.65 * abSpec) * clamp(uAnisoBrushedParams.w, 0.0, 3.0));\n" \
    "    }\n" \
    "    if (uCamValid != 0) {\n" \
    "        float ndv = clamp(dot(N, V), 0.0, 1.0);\n" \
    "        if (uXrayParams.x > 0.5 && uXrayParams.z > 0.0005) {\n" \
    "            float xrRim = pow(clamp(1.0 - ndv, 0.0, 1.0), clamp(uXrayParams.y, 0.5, 8.0));\n" \
    "            rgb += uXrayColor * (xrRim * clamp(uXrayParams.z, 0.0, 3.0));\n" \
    "        }\n" \
    "        if (uHemiOn != 0) {\n" \
    "            vec3 R = reflect(-V, N);\n" \
    "            vec3 envSpec = mix(uHemiGround, uHemiSky, clamp(R.y * 0.5 + 0.5, 0.0, 1.0));\n" \
    "            vec3 f0 = mix(vec3(0.04), baseRgb, metal);\n" \
    "            float fTerm = pow(1.0 - ndv, 5.0);\n" \
    "            vec3 fresnel = f0 + (vec3(1.0) - f0) * fTerm;\n" \
    "            float smoothness = 1.0 - rough;\n" \
    "            rgb += envSpec * fresnel * (smoothness * smoothness) * ao * 0.75;\n" \
    "        }\n" \
    "        if ((uRimLight.r + uRimLight.g + uRimLight.b) > 0.0001) {\n" \
    "            rgb += uRimLight.rgb * pow(1.0 - ndv, max(uRimLight.w, 0.25));\n" \
    "        }\n" \
    "    }\n" \
    "    if (uHoloScanParams.x > 0.5 && uHoloScanParams.w > 0.0001) {\n" \
    "        float sWave = 0.5 + 0.5 * sin(v_world_pos.y * uHoloScanParams.y + uHoloScanParams.z);\n" \
    "        float hFres = (uCamValid != 0) ? pow(1.0 - clamp(abs(dot(N, V)), 0.0, 1.0), 1.5) : 0.0;\n" \
    "        float hMask = clamp((0.2 + 0.8 * sWave) + hFres * 0.5, 0.0, 1.5);\n" \
    "        rgb += uHoloScanColor * (hMask * uHoloScanParams.w);\n" \
    "    }\n" \
    "    if (uHitFlash.a > 0.0001) {\n" \
    "        rgb = mix(rgb, uHitFlash.rgb, clamp(uHitFlash.a, 0.0, 1.0));\n" \
    "    }\n" \
"    if (uFogParams.x > 0.5 && uCamValid != 0) {\n" \
"        float fogDist = length(uCamPos - v_world_pos);\n" \
"        float fogT = clamp((fogDist - uFogParams.y) * uFogParams.z, 0.0, 1.0);\n" \
"        rgb = mix(rgb, uFogColor, fogT);\n" \
"    }\n" \
"    if (uHeightFogParams.w > 0.5) {\n" \
"        float hAbove = max(v_world_pos.y - uHeightFogParams.x, 0.0);\n" \
"        float hf = clamp(exp(-hAbove * max(uHeightFogParams.y, 0.001)) * clamp(uHeightFogParams.z, 0.0, 1.0), 0.0, 1.0);\n" \
"        rgb = mix(rgb, uHeightFogColor, hf);\n" \
"    }\n" \
"    frag = vec4(rgb, outAlpha);\n"

#define ZAN_GAME_3D_SAMPLE_ALBEDO \
"    vec4 tex;\n" \
"    if (uTriplanar.x > 0.5) {\n" \
"        float tSc = max(uTriplanar.y, 0.0001);\n" \
"        float tSh = max(uTriplanar.z, 1.0);\n" \
"        vec3 bw = pow(abs(normalize(v_normal)) + vec3(0.0001), vec3(tSh));\n" \
"        bw /= max(bw.x + bw.y + bw.z, 0.0001);\n" \
"        vec4 tx = texture(uTex, fract(v_world_pos.zy * tSc));\n" \
"        vec4 ty = texture(uTex, fract(v_world_pos.xz * tSc));\n" \
"        vec4 tz = texture(uTex, fract(v_world_pos.xy * tSc));\n" \
"        tex = tx * bw.x + ty * bw.y + tz * bw.z;\n" \
"    } else {\n" \
"        tex = texture(uTex, v_uv);\n" \
"    }\n"

static const char *const ZAN_GAME_3D_FS_PLAIN =
ZAN_GAME_3D_LIGHT_UNIFORMS
ZAN_GAME_3D_SHADOW_UNIFORMS
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_SHADOW_FACTOR
ZAN_GAME_3D_LIGHT_SHADOW_BODY
ZAN_GAME_3D_SAMPLE_ALBEDO
ZAN_GAME_3D_FINAL_COMPOSE("tex.rgb * uColor.rgb", "tex.a * uColor.a")
"}\n";

static const char *const ZAN_GAME_3D_FS_COLOR =
ZAN_GAME_3D_LIGHT_UNIFORMS
ZAN_GAME_3D_SHADOW_UNIFORMS
"uniform sampler2D uTex;\n"
"uniform vec4 uColor;\n"
"in vec2 v_uv;\n"
"in vec3 v_normal;\n"
"in vec4 v_color;\n"
"out vec4 frag;\n"
"void main() {\n"
ZAN_GAME_3D_SHADOW_FACTOR
ZAN_GAME_3D_LIGHT_SHADOW_BODY
ZAN_GAME_3D_SAMPLE_ALBEDO
ZAN_GAME_3D_FINAL_COMPOSE("tex.rgb * uColor.rgb * v_color.rgb", "tex.a * uColor.a * v_color.a")
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

static const char *const ZAN_GAME_POST_VS =
"out vec2 v_uv;\n"
"void main() {\n"
"    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,\n"
"                  (gl_VertexID == 2) ? 3.0 : -1.0);\n"
"    v_uv = p * 0.5 + 0.5;\n"
"    gl_Position = vec4(p, 0.0, 1.0);\n"
"}\n";

static const char *const ZAN_GAME_POST_FS =
"uniform sampler2D uScene;\n"
"uniform vec2 uTexelSize;\n"
"uniform float uExposure;\n"
"uniform int uTonemap;\n"
"uniform float uVignette;\n"
"uniform vec2 uBloom;\n"
"uniform vec2 uColorGrade;\n"
"uniform vec3 uColorTint;\n"
"uniform vec2 uPostLens;\n"
"uniform vec3 uPostFilm;\n"
"uniform vec4 uPostCinema;\n"
"uniform vec3 uPostRadialBlur;\n"
"uniform vec2 uPostPixelate;\n"
"uniform vec4 uPostDuotoneDark;\n"
"uniform vec3 uPostDuotoneLight;\n"
"uniform vec4 uPostCrt;\n"
"uniform vec2 uPostOutlineParams;\n"
"uniform vec3 uPostOutlineColor;\n"
"uniform vec3 uPostThermal;\n"
"uniform vec2 uPostFlashParams;\n"
"uniform vec3 uPostFlashColor;\n"
"uniform vec4 uPostNightVision;\n"
"uniform vec3 uPostHalftone;\n"
"uniform vec3 uPostGlitch;\n"
"uniform vec3 uPostTiltShift;\n"
"uniform vec2 uPostFrostParams;\n"
"uniform vec3 uPostFrostColor;\n"
"uniform vec3 uPostSepia;\n"
"uniform vec2 uPostStreakParams;\n"
"uniform vec3 uPostStreakColor;\n"
"uniform vec2 uPostSplitParams;\n"
"uniform vec3 uPostSplitShadow;\n"
"uniform vec3 uPostSplitHigh;\n"
"uniform vec2 uPostOilPaint;\n"
"uniform vec2 uPostScopeBlur;\n"
"uniform vec2 uPostDaltonize;\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"vec3 brightPass(vec3 c, float th) {\n"
"    float lum = max(c.r, max(c.g, c.b));\n"
"    float excess = max(lum - th, 0.0);\n"
"    return (lum > 0.0001) ? c * (excess / lum) : vec3(0.0);\n"
"}\n"
"void main() {\n"
"    vec2 uv = v_uv;\n"
"    if (uPostCinema.y > 0.0001) {\n"
"        float wOff = sin(uv.y * uPostCinema.z + uPostCinema.w) * uPostCinema.y;\n"
"        uv = clamp(vec2(uv.x + wOff, uv.y), vec2(0.0), vec2(1.0));\n"
"    }\n"
"    bool crtOutOfBounds = false;\n"
"    if (uPostCrt.z > 0.0005) {\n"
"        vec2 cc = uv * 2.0 - vec2(1.0);\n"
"        float r2 = dot(cc, cc);\n"
"        cc = cc * (1.0 + uPostCrt.z * r2);\n"
"        uv = cc * 0.5 + vec2(0.5);\n"
"        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
"            crtOutOfBounds = true;\n"
"        }\n"
"        uv = clamp(uv, vec2(0.0), vec2(1.0));\n"
"    }\n"
"    if (uPostPixelate.x >= 1.5) {\n"
"        vec2 stepUv = uTexelSize * uPostPixelate.x;\n"
"        uv = clamp((floor(uv / stepUv) + vec2(0.5)) * stepUv, vec2(0.0), vec2(1.0));\n"
"    }\n"
    "    vec4 src = texture(uScene, uv);\n"
    "    vec3 col = src.rgb;\n"
    "    if (uPostOilPaint.x > 0.0005) {\n"
    "        float opR = clamp(uPostOilPaint.y, 1.0, 4.0);\n"
    "        vec3 opBestMean = col;\n"
    "        float opBestVar = 1000000.0;\n"
    "        for (int qy = -1; qy <= 1; qy += 2) {\n"
    "            for (int qx = -1; qx <= 1; qx += 2) {\n"
    "                vec2 sx = vec2(float(qx) * uTexelSize.x * opR, 0.0);\n"
    "                vec2 sy = vec2(0.0, float(qy) * uTexelSize.y * opR);\n"
    "                vec3 c0 = texture(uScene, clamp(uv, vec2(0.0), vec2(1.0))).rgb;\n"
    "                vec3 c1 = texture(uScene, clamp(uv + sx, vec2(0.0), vec2(1.0))).rgb;\n"
    "                vec3 c2 = texture(uScene, clamp(uv + sy, vec2(0.0), vec2(1.0))).rgb;\n"
    "                vec3 c3 = texture(uScene, clamp(uv + sx + sy, vec2(0.0), vec2(1.0))).rgb;\n"
    "                vec3 qm = (c0 + c1 + c2 + c3) * 0.25;\n"
    "                vec3 qs = (c0 * c0 + c1 * c1 + c2 * c2 + c3 * c3) * 0.25;\n"
    "                float qv = dot(max(qs - qm * qm, vec3(0.0)), vec3(1.0));\n"
    "                if (qv < opBestVar) {\n"
    "                    opBestVar = qv;\n"
    "                    opBestMean = qm;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        col = mix(col, opBestMean, clamp(uPostOilPaint.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostScopeBlur.x > 0.0005) {\n"
    "        float sbRad = length((uv - vec2(0.5)) * 2.0);\n"
    "        float sbAmt = smoothstep(clamp(uPostScopeBlur.y, 0.1, 1.0), 1.15, sbRad) * clamp(uPostScopeBlur.x, 0.0, 1.0);\n"
    "        if (sbAmt > 0.001) {\n"
    "            vec2 sbStep = uTexelSize * (sbAmt * 4.0);\n"
    "            vec3 sbAcc = col * 2.0;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2( sbStep.x,  0.0), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2(-sbStep.x,  0.0), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2( 0.0,  sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2( 0.0, -sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2( sbStep.x,  sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2(-sbStep.x,  sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2( sbStep.x, -sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            sbAcc += texture(uScene, clamp(uv + vec2(-sbStep.x, -sbStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            col = mix(col, sbAcc * 0.1, sbAmt);\n"
    "        }\n"
    "    }\n"
    "    if (uPostGlitch.x > 0.0005) {\n"
    "        float gBand = floor(uv.y * max(uPostGlitch.y, 4.0));\n"
    "        float gHash = fract(sin(gBand * 127.1 + uPostGlitch.z * 311.7) * 43758.5453);\n"
    "        if (gHash > (1.0 - clamp(uPostGlitch.x, 0.0, 1.0) * 0.65)) {\n"
    "            float gDir = (fract(gHash * 19.3) > 0.5) ? 1.0 : -1.0;\n"
    "            float gShift = gDir * clamp(uPostGlitch.x, 0.0, 1.0) * 0.08;\n"
    "            float gSplit = clamp(uPostGlitch.x, 0.0, 1.0) * 0.045;\n"
    "            vec2 gUv = vec2(clamp(uv.x + gShift, 0.0, 1.0), uv.y);\n"
    "            col.r = texture(uScene, vec2(clamp(gUv.x + gSplit, 0.0, 1.0), gUv.y)).r;\n"
    "            col.g = texture(uScene, gUv).g;\n"
    "            col.b = texture(uScene, vec2(clamp(gUv.x - gSplit, 0.0, 1.0), gUv.y)).b;\n"
    "        }\n"
    "    }\n"
    "    if (uPostTiltShift.x > 0.0005) {\n"
    "        float tsDist = max(abs(uv.y - clamp(uPostTiltShift.y, 0.0, 1.0)) - clamp(uPostTiltShift.z, 0.02, 0.45), 0.0);\n"
    "        float tsAmt = clamp(tsDist * 4.0, 0.0, 1.0) * clamp(uPostTiltShift.x, 0.0, 1.0);\n"
    "        if (tsAmt > 0.001) {\n"
    "            vec2 tsStep = uTexelSize * (tsAmt * 4.5);\n"
    "            vec3 tsAcc = col * 2.0;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2( tsStep.x,  0.0), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2(-tsStep.x,  0.0), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2( 0.0,  tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2( 0.0, -tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2( tsStep.x,  tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2(-tsStep.x,  tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2( tsStep.x, -tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            tsAcc += texture(uScene, clamp(uv + vec2(-tsStep.x, -tsStep.y), vec2(0.0), vec2(1.0))).rgb;\n"
    "            col = mix(col, tsAcc * 0.1, tsAmt);\n"
    "        }\n"
    "    }\n"
    "    if (uPostRadialBlur.x > 0.0005) {\n"
"        vec2 rDir = uv - uPostRadialBlur.yz;\n"
"        vec3 rAcc = col;\n"
"        for (int ri = 1; ri <= 4; ++ri) {\n"
"            float rf = float(ri) * 0.25 * uPostRadialBlur.x;\n"
"            rAcc += texture(uScene, clamp(uv - rDir * rf, vec2(0.0), vec2(1.0))).rgb;\n"
"        }\n"
"        col = rAcc * 0.2;\n"
"    }\n"
"    if (uPostLens.x > 0.001) {\n"
"        vec3 rgbN = texture(uScene, uv + vec2(0.0, -uTexelSize.y)).rgb;\n"
"        vec3 rgbS = texture(uScene, uv + vec2(0.0,  uTexelSize.y)).rgb;\n"
"        vec3 rgbW = texture(uScene, uv + vec2(-uTexelSize.x, 0.0)).rgb;\n"
"        vec3 rgbE = texture(uScene, uv + vec2( uTexelSize.x, 0.0)).rgb;\n"
"        vec3 lw = vec3(0.299, 0.587, 0.114);\n"
"        float lC = dot(col, lw);\n"
"        float lN = dot(rgbN, lw);\n"
"        float lS = dot(rgbS, lw);\n"
"        float lW = dot(rgbW, lw);\n"
"        float lE = dot(rgbE, lw);\n"
"        float lMin = min(lC, min(min(lN, lS), min(lW, lE)));\n"
"        float lMax = max(lC, max(max(lN, lS), max(lW, lE)));\n"
"        float contrast = lMax - lMin;\n"
"        if (contrast > max(0.06, lMax * 0.125)) {\n"
"            float edgeH = abs(lN + lS - 2.0 * lC);\n"
"            float edgeV = abs(lW + lE - 2.0 * lC);\n"
"            vec3 avg = (edgeH >= edgeV) ? 0.5 * (rgbN + rgbS) : 0.5 * (rgbW + rgbE);\n"
"            float blendF = clamp(contrast * 1.5, 0.0, 0.75) * clamp(uPostLens.x, 0.0, 1.0);\n"
"            col = mix(col, avg, blendF);\n"
"        }\n"
"    }\n"
    "    if (uPostLens.y > 0.0002) {\n"
    "        vec2 dir = uv - vec2(0.5);\n"
    "        vec2 off = dir * uPostLens.y;\n"
    "        col.r = texture(uScene, clamp(uv + off, vec2(0.0), vec2(1.0))).r;\n"
    "        col.b = texture(uScene, clamp(uv - off, vec2(0.0), vec2(1.0))).b;\n"
    "    }\n"
    "    if (uPostFilm.x > 0.001) {\n"
    "        vec3 sN = texture(uScene, uv + vec2(0.0, -uTexelSize.y)).rgb;\n"
    "        vec3 sS = texture(uScene, uv + vec2(0.0,  uTexelSize.y)).rgb;\n"
    "        vec3 sW = texture(uScene, uv + vec2(-uTexelSize.x, 0.0)).rgb;\n"
    "        vec3 sE = texture(uScene, uv + vec2( uTexelSize.x, 0.0)).rgb;\n"
    "        vec3 lap = 4.0 * col - sN - sS - sW - sE;\n"
    "        col = max(col + lap * uPostFilm.x, vec3(0.0));\n"
    "    }\n"
    "    if (uBloom.y > 0.001) {\n"
"        float th = uBloom.x;\n"
"        vec3 glow = brightPass(col, th) * 0.16;\n"
"        vec2 t2 = uTexelSize * 2.0;\n"
"        vec2 t5 = uTexelSize * 5.0;\n"
"        vec2 t9 = uTexelSize * 9.0;\n"
"        glow += brightPass(texture(uScene, uv + vec2( t2.x, 0.0)).rgb, th) * 0.10;\n"
"        glow += brightPass(texture(uScene, uv + vec2(-t2.x, 0.0)).rgb, th) * 0.10;\n"
"        glow += brightPass(texture(uScene, uv + vec2(0.0,  t2.y)).rgb, th) * 0.10;\n"
"        glow += brightPass(texture(uScene, uv + vec2(0.0, -t2.y)).rgb, th) * 0.10;\n"
"        glow += brightPass(texture(uScene, uv + vec2( t5.x,  t5.y)).rgb, th) * 0.07;\n"
"        glow += brightPass(texture(uScene, uv + vec2(-t5.x,  t5.y)).rgb, th) * 0.07;\n"
"        glow += brightPass(texture(uScene, uv + vec2( t5.x, -t5.y)).rgb, th) * 0.07;\n"
"        glow += brightPass(texture(uScene, uv + vec2(-t5.x, -t5.y)).rgb, th) * 0.07;\n"
"        glow += brightPass(texture(uScene, uv + vec2( t9.x, 0.0)).rgb, th) * 0.04;\n"
"        glow += brightPass(texture(uScene, uv + vec2(-t9.x, 0.0)).rgb, th) * 0.04;\n"
"        glow += brightPass(texture(uScene, uv + vec2(0.0,  t9.y)).rgb, th) * 0.04;\n"
"        glow += brightPass(texture(uScene, uv + vec2(0.0, -t9.y)).rgb, th) * 0.04;\n"
"        col += glow * uBloom.y;\n"
"    }\n"
"    col *= max(uExposure, 0.0);\n"
    "    if (uTonemap == 1) {\n"
    "        col = col / (vec3(1.0) + col);\n"
    "    } else if (uTonemap == 2) {\n"
    "        col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);\n"
    "    }\n"
    "    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "    col = mix(vec3(luma), col, uColorGrade.x);\n"
    "    col = (col - vec3(0.5)) * uColorGrade.y + vec3(0.5);\n"
    "    col *= uColorTint;\n"
    "    if (uPostDuotoneDark.w > 0.0005) {\n"
    "        float dLum = clamp(dot(col, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);\n"
    "        vec3 duo = mix(uPostDuotoneDark.rgb, uPostDuotoneLight, dLum);\n"
    "        col = mix(col, duo, clamp(uPostDuotoneDark.w, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostOutlineParams.x > 0.0005) {\n"
    "        vec3 oL = vec3(0.299, 0.587, 0.114);\n"
    "        float lN = dot(texture(uScene, clamp(uv + vec2(0.0, -uTexelSize.y), vec2(0.0), vec2(1.0))).rgb, oL);\n"
    "        float lS = dot(texture(uScene, clamp(uv + vec2(0.0,  uTexelSize.y), vec2(0.0), vec2(1.0))).rgb, oL);\n"
    "        float lW = dot(texture(uScene, clamp(uv + vec2(-uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb, oL);\n"
    "        float lE = dot(texture(uScene, clamp(uv + vec2( uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb, oL);\n"
    "        float ogx = lE - lW;\n"
    "        float ogy = lS - lN;\n"
    "        float oMag = sqrt(ogx * ogx + ogy * ogy);\n"
    "        float oMask = smoothstep(uPostOutlineParams.y, uPostOutlineParams.y + 0.08, oMag);\n"
    "        col = mix(col, uPostOutlineColor, oMask * clamp(uPostOutlineParams.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostThermal.x > 0.0005) {\n"
    "        float tLum = dot(col, vec3(0.299, 0.587, 0.114));\n"
    "        float tVal = clamp(tLum * max(uPostThermal.y, 0.1) + uPostThermal.z, 0.0, 1.0);\n"
    "        vec3 c0 = vec3(0.04, 0.06, 0.45);\n"
    "        vec3 c1 = vec3(0.48, 0.08, 0.65);\n"
    "        vec3 c2 = vec3(0.95, 0.25, 0.05);\n"
    "        vec3 c3 = vec3(1.00, 0.92, 0.15);\n"
    "        vec3 c4 = vec3(1.00, 1.00, 1.00);\n"
    "        vec3 tCol = tVal < 0.25 ? mix(c0, c1, tVal * 4.0)\n"
    "            : (tVal < 0.50 ? mix(c1, c2, (tVal - 0.25) * 4.0)\n"
    "            : (tVal < 0.75 ? mix(c2, c3, (tVal - 0.50) * 4.0)\n"
    "            : mix(c3, c4, (tVal - 0.75) * 4.0)));\n"
    "        col = mix(col, tCol, clamp(uPostThermal.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostFlashParams.x > 0.0005 || uPostFlashParams.y > 0.0005) {\n"
    "        vec2 fd = uv - vec2(0.5);\n"
    "        float fDist = length(fd) * 2.0;\n"
    "        float fEdge = smoothstep(0.45, 1.35, fDist) * clamp(uPostFlashParams.x, 0.0, 1.0);\n"
    "        col = mix(col, uPostFlashColor, fEdge);\n"
    "        col = mix(col, uPostFlashColor, clamp(uPostFlashParams.y, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostNightVision.x > 0.0005) {\n"
    "        float nvLum = clamp(dot(col, vec3(0.299, 0.587, 0.114)) * max(uPostNightVision.y, 0.25), 0.0, 1.0);\n"
    "        float nvN = (fract(sin(dot(uv * 512.0 + vec2(uPostNightVision.w * 23.1, uPostNightVision.w * 41.7), vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * uPostNightVision.z;\n"
    "        float nvSig = clamp(nvLum + nvN, 0.0, 1.0);\n"
    "        vec3 nvPhos = vec3(nvSig * 0.12 + nvSig * nvSig * 0.20, clamp(nvSig * 1.15, 0.0, 1.0), nvSig * 0.24 + nvSig * nvSig * 0.16);\n"
    "        float nvRad = length((uv - vec2(0.5)) * 2.0);\n"
    "        float nvMask = smoothstep(0.96, 0.76, nvRad);\n"
    "        col = mix(col, nvPhos * nvMask, clamp(uPostNightVision.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostHalftone.x > 0.0005) {\n"
    "        vec2 htPx = uv / max(uTexelSize, vec2(0.0001));\n"
    "        vec2 htRot = vec2(htPx.x + htPx.y, htPx.y - htPx.x) * 0.70710678;\n"
    "        vec2 htCell = fract(htRot / max(uPostHalftone.y, 2.0)) - vec2(0.5);\n"
    "        float htDist = length(htCell);\n"
    "        float htLum = clamp(dot(col, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);\n"
    "        float htAdj = pow(htLum, clamp(uPostHalftone.z, 0.25, 3.0));\n"
    "        float htDotRad = sqrt(clamp(1.0 - htAdj, 0.0, 1.0)) * 0.62;\n"
    "        float htInk = smoothstep(htDotRad + 0.05, htDotRad - 0.05, htDist);\n"
    "        vec3 htCol = mix(clamp(col * 1.15 + vec3(0.06), 0.0, 1.0), col * 0.08, htInk);\n"
    "        col = mix(col, htCol, clamp(uPostHalftone.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostStreakParams.x > 0.0005) {\n"
    "        vec3 stAcc = vec3(0.0);\n"
    "        float stTh = clamp(uPostStreakParams.y, 0.0, 2.0);\n"
    "        for (int si = 1; si <= 4; ++si) {\n"
    "            float sf = float(si);\n"
    "            float sw = 1.0 - sf * 0.18;\n"
    "            float sOff = uTexelSize.x * (sf * 5.0);\n"
    "            stAcc += brightPass(texture(uScene, clamp(vec2(uv.x + sOff, uv.y), vec2(0.0), vec2(1.0))).rgb, stTh) * sw;\n"
    "            stAcc += brightPass(texture(uScene, clamp(vec2(uv.x - sOff, uv.y), vec2(0.0), vec2(1.0))).rgb, stTh) * sw;\n"
    "        }\n"
    "        float stLum = dot(stAcc, vec3(0.299, 0.587, 0.114));\n"
    "        col += uPostStreakColor * (stLum * 0.35 * clamp(uPostStreakParams.x, 0.0, 3.0));\n"
    "    }\n"
    "    if (uPostSepia.x > 0.0005) {\n"
    "        vec3 sepCol = vec3(\n"
    "            dot(col, vec3(0.393, 0.769, 0.189)),\n"
    "            dot(col, vec3(0.349, 0.686, 0.168)),\n"
    "            dot(col, vec3(0.272, 0.534, 0.131)));\n"
    "        if (uPostSepia.y > 0.0005) {\n"
    "            float scBand = floor(uv.x * 180.0);\n"
    "            float scHash = fract(sin(scBand * 91.7 + uPostSepia.z * 17.3) * 43758.5453);\n"
    "            if (scHash > (1.0 - clamp(uPostSepia.y, 0.0, 1.0) * 0.12)) {\n"
    "                sepCol = mix(sepCol, vec3(0.95, 0.90, 0.78), 0.55);\n"
    "            }\n"
    "        }\n"
    "        col = mix(col, clamp(sepCol, 0.0, 1.0), clamp(uPostSepia.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostSplitParams.x > 0.0005) {\n"
    "        float stLum = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "        float stMask = smoothstep(0.01, 0.08, stLum);\n"
    "        float stPivot = clamp(0.5 - uPostSplitParams.y * 0.35, 0.15, 0.85);\n"
    "        float stHighW = smoothstep(stPivot - 0.18, stPivot + 0.18, stLum);\n"
    "        vec3 stShadowCol = col + uPostSplitShadow * (0.45 * (1.0 - stLum) * stMask);\n"
    "        vec3 stHighCol = col * mix(vec3(1.0), uPostSplitHigh * 1.4, 0.75);\n"
    "        vec3 stShaded = mix(stShadowCol, stHighCol, stHighW);\n"
    "        col = mix(col, clamp(stShaded, 0.0, 1.0), clamp(uPostSplitParams.x, 0.0, 1.0));\n"
    "    }\n"
    "    if (uPostFrostParams.x > 0.0005) {\n"
    "        vec2 fd = (v_uv - vec2(0.5)) * 2.0;\n"
    "        float fRad = length(fd);\n"
    "        float fMask = smoothstep(clamp(uPostFrostParams.y, 0.15, 0.95), 1.35, fRad);\n"
    "        if (fMask > 0.001) {\n"
    "            float fCrystal = 0.72 + 0.28 * fract(sin(dot(floor(v_uv * 96.0), vec2(127.1, 311.7))) * 43758.5453);\n"
    "            float fAmt = clamp(fMask * fCrystal * clamp(uPostFrostParams.x, 0.0, 1.0), 0.0, 1.0);\n"
    "            col = mix(col, uPostFrostColor, fAmt);\n"
    "        }\n"
    "    }\n"
    "    if (uVignette > 0.001) {\n"
    "        vec2 d = (v_uv - vec2(0.5)) * 2.0;\n"
    "        float vig = clamp(1.0 - uVignette * dot(d, d) * 0.5, 0.0, 1.0);\n"
    "        col *= vig;\n"
    "    }\n"
    "    if (uPostFilm.y > 0.0005) {\n"
    "        float gNoise = fract(sin(dot(v_uv * 1024.0 + vec2(uPostFilm.z * 17.3, uPostFilm.z * 31.7), vec2(12.9898, 78.233))) * 43758.5453);\n"
    "        col += vec3((gNoise - 0.5) * uPostFilm.y);\n"
    "    }\n"
    "    if (uPostPixelate.y >= 2.0) {\n"
    "        float lv = floor(uPostPixelate.y) - 1.0;\n"
    "        col = floor(clamp(col, 0.0, 1.0) * lv + 0.5) / lv;\n"
    "    }\n"
    "    if (uPostCrt.x > 0.0005) {\n"
    "        float sWave = 0.5 + 0.5 * sin(uv.y * uPostCrt.y * 6.2831853);\n"
    "        float sMul = (1.0 - clamp(uPostCrt.x, 0.0, 0.95) * (1.0 - sWave)) * max(uPostCrt.w, 0.1);\n"
    "        col *= sMul;\n"
    "    }\n"
    "    if (crtOutOfBounds) {\n"
    "        col = vec3(0.0);\n"
    "    }\n"
    "    if (uPostCinema.x > 0.0005) {\n"
    "        if (v_uv.y < uPostCinema.x || v_uv.y > (1.0 - uPostCinema.x)) {\n"
    "            col = vec3(0.0);\n"
    "        }\n"
    "    }\n"
    "    if (uPostDaltonize.x > 0.5 && uPostDaltonize.y > 0.0005) {\n"
    "        vec3 dCol = clamp(col, 0.0, 1.0);\n"
    "        float dL = 17.8824 * dCol.r + 43.5161 * dCol.g + 4.11935 * dCol.b;\n"
    "        float dM = 3.45565 * dCol.r + 27.1554 * dCol.g + 3.86714 * dCol.b;\n"
    "        float dS = 0.0299566 * dCol.r + 0.184309 * dCol.g + 1.46709 * dCol.b;\n"
    "        float sL = dL;\n"
    "        float sM = dM;\n"
    "        float sS = dS;\n"
    "        if (uPostDaltonize.x < 1.5) {\n"
    "            sL = 2.02344 * dM - 2.52581 * dS;\n"
    "        } else if (uPostDaltonize.x < 2.5) {\n"
    "            sM = 0.494207 * dL + 1.24827 * dS;\n"
    "        } else {\n"
    "            sS = -0.395913 * dL + 0.801109 * dM;\n"
    "        }\n"
    "        vec3 simRgb;\n"
    "        simRgb.r = 0.0809444 * sL - 0.130504 * sM + 0.116721 * sS;\n"
    "        simRgb.g = -0.0102485 * sL + 0.0540193 * sM - 0.113615 * sS;\n"
    "        simRgb.b = -0.000365297 * sL - 0.00412161 * sM + 0.693511 * sS;\n"
    "        vec3 dErr = dCol - simRgb;\n"
    "        vec3 dShift = vec3(0.0, 0.7 * dErr.r + dErr.g, 0.7 * dErr.r + dErr.b);\n"
    "        col = mix(dCol, clamp(dCol + dShift, 0.0, 1.0), clamp(uPostDaltonize.y, 0.0, 1.0));\n"
    "    }\n"
    "    frag = vec4(clamp(col, 0.0, 1.0), src.a);\n"
"}\n";

typedef struct {
    zgl_uint program;
    zgl_int u_scene;
    zgl_int u_texel_size;
    zgl_int u_exposure;
    zgl_int u_tonemap;
    zgl_int u_vignette;
    zgl_int u_bloom;
    zgl_int u_color_grade;
    zgl_int u_color_tint;
    zgl_int u_post_lens;
    zgl_int u_post_film;
    zgl_int u_post_cinema;
    zgl_int u_post_radial_blur;
    zgl_int u_post_pixelate;
    zgl_int u_post_duotone_dark;
    zgl_int u_post_duotone_light;
    zgl_int u_post_crt;
    zgl_int u_post_outline_params;
    zgl_int u_post_outline_color;
    zgl_int u_post_thermal;
    zgl_int u_post_flash_params;
    zgl_int u_post_flash_color;
    zgl_int u_post_night_vision;
    zgl_int u_post_halftone;
    zgl_int u_post_glitch;
    zgl_int u_post_tilt_shift;
    zgl_int u_post_frost_params;
    zgl_int u_post_frost_color;
    zgl_int u_post_sepia;
    zgl_int u_post_streak_params;
    zgl_int u_post_streak_color;
    zgl_int u_post_split_params;
    zgl_int u_post_split_shadow;
    zgl_int u_post_split_high;
    zgl_int u_post_oil_paint;
    zgl_int u_post_scope_blur;
    zgl_int u_post_daltonize;
} zan_game_post_program;

static const char *const ZAN_GAME_SKYBOX_FS =
"uniform vec3 uCamRight;\n"
"uniform vec3 uCamUp;\n"
"uniform vec3 uCamForward;\n"
"uniform vec2 uProj;\n"
"uniform vec3 uSunDir;\n"
"uniform vec3 uSkyTop;\n"
"uniform vec3 uSkyHorizon;\n"
"uniform vec3 uGround;\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"void main() {\n"
"    float ndcX = v_uv.x * 2.0 - 1.0;\n"
"    float ndcY = v_uv.y * 2.0 - 1.0;\n"
"    vec3 rd = normalize(uCamForward + uCamRight * (ndcX * uProj.x) + uCamUp * (ndcY * uProj.y));\n"
"    vec3 col;\n"
"    if (rd.y >= 0.0) {\n"
"        float t = pow(clamp(rd.y, 0.0, 1.0), 0.55);\n"
"        col = mix(uSkyHorizon, uSkyTop, t);\n"
"    } else {\n"
"        float t = pow(clamp(-rd.y, 0.0, 1.0), 0.45);\n"
"        col = mix(uSkyHorizon, uGround, t);\n"
"    }\n"
"    vec3 sdir = normalize(uSunDir);\n"
"    float sunDot = max(dot(rd, sdir), 0.0);\n"
"    float sunHalo = pow(sunDot, 24.0) * 0.35;\n"
"    float sunDisk = smoothstep(0.9965, 0.9992, sunDot) * 1.4;\n"
"    col += vec3(1.0, 0.94, 0.80) * (sunHalo + sunDisk);\n"
"    frag = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
"}\n";

typedef struct {
    zgl_uint program;
    zgl_int u_cam_right;
    zgl_int u_cam_up;
    zgl_int u_cam_forward;
    zgl_int u_proj;
    zgl_int u_sun_dir;
    zgl_int u_sky_top;
    zgl_int u_sky_horizon;
    zgl_int u_ground;
} zan_game_skybox_program;

static zan_game_post_program g_game_post_prog;
static zan_game_skybox_program g_game_skybox_prog;
static zgl_uint g_game_post_fbo;
static zgl_uint g_game_post_tex;
static zgl_uint g_game_post_vao;
static int g_game_post_w;
static int g_game_post_h;

static void zan_game_drop_post(void) {
    if (g_game_gl) {
        if (g_game_post_prog.program)
            g_game_gl->DeleteProgram(g_game_post_prog.program);
        if (g_game_skybox_prog.program)
            g_game_gl->DeleteProgram(g_game_skybox_prog.program);
        if (g_game_post_fbo) g_game_gl->DeleteFramebuffers(1, &g_game_post_fbo);
        if (g_game_post_tex) g_game_gl->DeleteTextures(1, &g_game_post_tex);
        if (g_game_post_vao) g_game_gl->DeleteVertexArrays(1, &g_game_post_vao);
    }
    memset(&g_game_post_prog, 0, sizeof(g_game_post_prog));
    memset(&g_game_skybox_prog, 0, sizeof(g_game_skybox_prog));
    g_game_post_fbo = 0;
    g_game_post_tex = 0;
    g_game_post_vao = 0;
    g_game_post_w = 0;
    g_game_post_h = 0;
}

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

static void zan_game_drop_shadow(void) {
    if (g_game_gl) {
        if (g_game_shadow_fbo) g_game_gl->DeleteFramebuffers(1, &g_game_shadow_fbo);
        if (g_game_shadow_tex) g_game_gl->DeleteTextures(1, &g_game_shadow_tex);
    }
    g_game_shadow_fbo = 0;
    g_game_shadow_tex = 0;
    g_game_shadow_size = 0;
    g_game_shadow_ready = 0;
    g_game_shadow_surface = -1;
}

static void zan_game_forget_gpu(void) {
    if (g_game_gl) {
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                if (g_game_progs[r][c].program)
                    g_game_gl->DeleteProgram(g_game_progs[r][c].program);
        if (g_game_skin_prog.program)
            g_game_gl->DeleteProgram(g_game_skin_prog.program);
        if (g_game_shadow_prog.program)
            g_game_gl->DeleteProgram(g_game_shadow_prog.program);
        if (g_game_shadow_skin_prog.program)
            g_game_gl->DeleteProgram(g_game_shadow_skin_prog.program);
        if (g_game_instance_vbo)
            g_game_gl->DeleteBuffers(1, &g_game_instance_vbo);
        zan_game_drop_shadow();
        zan_game_drop_post();
    }
    if (g_game_meshes) {
        memset(g_game_meshes, 0, (size_t)g_game_mesh_cap * sizeof(zan_game_mesh));
    }
    memset(g_game_depth, 0, sizeof(g_game_depth));
    memset(g_game_progs, 0, sizeof(g_game_progs));
    memset(&g_game_skin_prog, 0, sizeof(g_game_skin_prog));
    memset(&g_game_shadow_prog, 0, sizeof(g_game_shadow_prog));
    memset(&g_game_shadow_skin_prog, 0, sizeof(g_game_shadow_skin_prog));
    g_game_instance_vbo = 0;
    g_game_batch_surface = -1;
    g_game_shadow_surface = -1;
    g_game_shadow_ready = 0;
    g_game_preserve_depth = 0;
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
    char vs_src[4096];
    char fs_src[32768];
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
    zgl_int model = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uModel");
    zgl_int light_vp = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uLightVP");
    zgl_int shadow_map = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uShadowMap");
    zgl_int shadow_on = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uShadowOn");
    zgl_int normal_map = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uNormalMap");
    zgl_int normal_on = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uNormalOn");
    zgl_int normal_scale = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uNormalScale");
    zgl_int orm_map = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uOrmMap");
    zgl_int orm_on = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uOrmOn");
    zgl_int ao_strength = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uAoStrength");
    zgl_int alpha_cutoff = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uAlphaCutoff");
    zgl_int emissive_map = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uEmissiveMap");
    zgl_int emissive_on = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uEmissiveOn");
    zgl_int uv_transform = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uUvTransform");
    zgl_int rim_light = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uRimLight");
    zgl_int toon = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uToon");
    zgl_int dissolve = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uDissolve");
    zgl_int dissolve_edge = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uDissolveEdge");
    zgl_int clip_plane = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uClipPlane");
    zgl_int clip_on = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uClipOn");
    zgl_int triplanar = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uTriplanar");
    zgl_int height_fog_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHeightFogParams");
    zgl_int height_fog_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHeightFogColor");
    zgl_int hit_flash = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHitFlash");
    zgl_int holo_scan_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHoloScanParams");
    zgl_int holo_scan_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHoloScanColor");
    zgl_int top_cover_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uTopCoverParams");
    zgl_int top_cover_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uTopCoverColor");
    zgl_int wind_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uWindParams");
    zgl_int dist_fade = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uDistFade");
    zgl_int world_grid_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uWorldGridParams");
    zgl_int world_grid_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uWorldGridColor");
    zgl_int caustics_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uCausticsParams");
    zgl_int caustics_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uCausticsColor");
    zgl_int ice_frost_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uIceFrostParams");
    zgl_int ice_frost_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uIceFrostColor");
    zgl_int lava_crust_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uLavaCrustParams");
    zgl_int lava_crust_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uLavaCrustColor");
    zgl_int iridescence_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uIridescenceParams");
    zgl_int sonar_origin_radius = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uSonarOriginRadius");
    zgl_int sonar_style = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uSonarStyle");
    zgl_int sonar_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uSonarColor");
    zgl_int force_field_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uForceFieldParams");
    zgl_int force_field_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uForceFieldColor");
    zgl_int rain_ripples_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uRainRipplesParams");
    zgl_int rain_ripples_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uRainRipplesColor");
    zgl_int xray_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uXrayParams");
    zgl_int xray_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uXrayColor");
    zgl_int portal_vortex_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uPortalVortexParams");
    zgl_int portal_vortex_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uPortalVortexColor");
    zgl_int blueprint_wire_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uBlueprintWireParams");
    zgl_int blueprint_wire_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uBlueprintWireColor");
    zgl_int heat_haze_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHeatHazeParams");
    zgl_int heat_haze_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uHeatHazeColor");
    zgl_int pixel_dither_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uPixelDitherParams");
    zgl_int crosshatch_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uCrosshatchParams");
    zgl_int crosshatch_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uCrosshatchColor");
    zgl_int aniso_brushed_params = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uAnisoBrushedParams");
    zgl_int aniso_brushed_color = instanced ? -1
        : g_game_gl->GetUniformLocation(program, "uAnisoBrushedColor");
    zgl_int color = instanced ? 0
        : g_game_gl->GetUniformLocation(program, "uColor");
    zgl_int light_dir = g_game_gl->GetUniformLocation(program, "uLightDir[0]");
    zgl_int light_color =
        g_game_gl->GetUniformLocation(program, "uLightColor[0]");
    zgl_int light_count = g_game_gl->GetUniformLocation(program, "uLightCount");
    zgl_int ambient = g_game_gl->GetUniformLocation(program, "uAmbient");
    zgl_int cam_pos = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uCamPos");
    zgl_int cam_valid = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uCamValid");
    zgl_int material = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uMaterial");
    zgl_int emissive = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uEmissive");
    zgl_int point_pos = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uPointPos[0]");
    zgl_int point_color = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uPointColor[0]");
    zgl_int point_count = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uPointCount");
    zgl_int spot_pos = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uSpotPos[0]");
    zgl_int spot_dir = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uSpotDir[0]");
    zgl_int spot_color = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uSpotColor[0]");
    zgl_int spot_count = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uSpotCount");
    zgl_int hemi_sky = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uHemiSky");
    zgl_int hemi_ground = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uHemiGround");
    zgl_int hemi_on = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uHemiOn");
    zgl_int fog_color = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uFogColor");
    zgl_int fog_params = instanced ? -1 : g_game_gl->GetUniformLocation(program, "uFogParams");
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
    prog->u_model = model;
    prog->u_light_vp = light_vp;
    prog->u_tex = tex;
    prog->u_shadow_map = shadow_map;
    prog->u_shadow_on = shadow_on;
    prog->u_normal_map = normal_map;
    prog->u_normal_on = normal_on;
    prog->u_normal_scale = normal_scale;
    prog->u_orm_map = orm_map;
    prog->u_orm_on = orm_on;
    prog->u_ao_strength = ao_strength;
    prog->u_alpha_cutoff = alpha_cutoff;
    prog->u_emissive_map = emissive_map;
    prog->u_emissive_on = emissive_on;
    prog->u_uv_transform = uv_transform;
    prog->u_rim_light = rim_light;
    prog->u_toon = toon;
    prog->u_dissolve = dissolve;
    prog->u_dissolve_edge = dissolve_edge;
    prog->u_clip_plane = clip_plane;
    prog->u_clip_on = clip_on;
    prog->u_triplanar = triplanar;
    prog->u_height_fog_params = height_fog_params;
    prog->u_height_fog_color = height_fog_color;
    prog->u_hit_flash = hit_flash;
    prog->u_holo_scan_params = holo_scan_params;
    prog->u_holo_scan_color = holo_scan_color;
    prog->u_top_cover_params = top_cover_params;
    prog->u_top_cover_color = top_cover_color;
    prog->u_wind_params = wind_params;
    prog->u_dist_fade = dist_fade;
    prog->u_world_grid_params = world_grid_params;
    prog->u_world_grid_color = world_grid_color;
    prog->u_caustics_params = caustics_params;
    prog->u_caustics_color = caustics_color;
    prog->u_ice_frost_params = ice_frost_params;
    prog->u_ice_frost_color = ice_frost_color;
    prog->u_lava_crust_params = lava_crust_params;
    prog->u_lava_crust_color = lava_crust_color;
    prog->u_iridescence_params = iridescence_params;
    prog->u_sonar_origin_radius = sonar_origin_radius;
    prog->u_sonar_style = sonar_style;
    prog->u_sonar_color = sonar_color;
    prog->u_force_field_params = force_field_params;
    prog->u_force_field_color = force_field_color;
    prog->u_rain_ripples_params = rain_ripples_params;
    prog->u_rain_ripples_color = rain_ripples_color;
    prog->u_xray_params = xray_params;
    prog->u_xray_color = xray_color;
    prog->u_portal_vortex_params = portal_vortex_params;
    prog->u_portal_vortex_color = portal_vortex_color;
    prog->u_blueprint_wire_params = blueprint_wire_params;
    prog->u_blueprint_wire_color = blueprint_wire_color;
    prog->u_heat_haze_params = heat_haze_params;
    prog->u_heat_haze_color = heat_haze_color;
    prog->u_pixel_dither_params = pixel_dither_params;
    prog->u_crosshatch_params = crosshatch_params;
    prog->u_crosshatch_color = crosshatch_color;
    prog->u_aniso_brushed_params = aniso_brushed_params;
    prog->u_aniso_brushed_color = aniso_brushed_color;
    prog->u_color = color;
    prog->u_light_dir = light_dir;
    prog->u_light_color = light_color;
    prog->u_light_count = light_count;
    prog->u_ambient = ambient;
    prog->u_cam_pos = cam_pos;
    prog->u_cam_valid = cam_valid;
    prog->u_material = material;
    prog->u_emissive = emissive;
    prog->u_point_pos = point_pos;
    prog->u_point_color = point_color;
    prog->u_point_count = point_count;
    prog->u_spot_pos = spot_pos;
    prog->u_spot_dir = spot_dir;
    prog->u_spot_color = spot_color;
    prog->u_spot_count = spot_count;
    prog->u_hemi_sky = hemi_sky;
    prog->u_hemi_ground = hemi_ground;
    prog->u_hemi_on = hemi_on;
    prog->u_fog_color = fog_color;
    prog->u_fog_params = fog_params;
    prog->u_bones = -1;
    prog->light_version = 0;
    return prog;
}

static zan_game_program *zan_game_skin_program_get(void) {
    if (g_game_skin_prog.program) return &g_game_skin_prog;
    char vs_src[4096];
    char fs_src[32768];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, ZAN_GAME_3D_VS_SKINNED);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, ZAN_GAME_3D_FS_PLAIN);
    zgl_uint program = zan_gui_gpu_program(vs_src, fs_src);
    if (!program) return NULL;
    zgl_int pos = g_game_gl->GetAttribLocation(program, "a_pos");
    zgl_int normal = g_game_gl->GetAttribLocation(program, "a_normal");
    zgl_int uv = g_game_gl->GetAttribLocation(program, "a_uv");
    zgl_int joints = g_game_gl->GetAttribLocation(program, "a_joints");
    zgl_int weights = g_game_gl->GetAttribLocation(program, "a_weights");
    zgl_int mvp = g_game_gl->GetUniformLocation(program, "uMVP");
    zgl_int model = g_game_gl->GetUniformLocation(program, "uModel");
    zgl_int light_vp = g_game_gl->GetUniformLocation(program, "uLightVP");
    zgl_int bones = g_game_gl->GetUniformLocation(program, "uBones[0]");
    zgl_int tex = g_game_gl->GetUniformLocation(program, "uTex");
    zgl_int shadow_map = g_game_gl->GetUniformLocation(program, "uShadowMap");
    zgl_int shadow_on = g_game_gl->GetUniformLocation(program, "uShadowOn");
    zgl_int normal_map = g_game_gl->GetUniformLocation(program, "uNormalMap");
    zgl_int normal_on = g_game_gl->GetUniformLocation(program, "uNormalOn");
    zgl_int normal_scale = g_game_gl->GetUniformLocation(program, "uNormalScale");
    zgl_int orm_map = g_game_gl->GetUniformLocation(program, "uOrmMap");
    zgl_int orm_on = g_game_gl->GetUniformLocation(program, "uOrmOn");
    zgl_int ao_strength = g_game_gl->GetUniformLocation(program, "uAoStrength");
    zgl_int alpha_cutoff = g_game_gl->GetUniformLocation(program, "uAlphaCutoff");
    zgl_int emissive_map = g_game_gl->GetUniformLocation(program, "uEmissiveMap");
    zgl_int emissive_on = g_game_gl->GetUniformLocation(program, "uEmissiveOn");
    zgl_int uv_transform = g_game_gl->GetUniformLocation(program, "uUvTransform");
    zgl_int rim_light = g_game_gl->GetUniformLocation(program, "uRimLight");
    zgl_int toon = g_game_gl->GetUniformLocation(program, "uToon");
    zgl_int dissolve = g_game_gl->GetUniformLocation(program, "uDissolve");
    zgl_int dissolve_edge = g_game_gl->GetUniformLocation(program, "uDissolveEdge");
    zgl_int clip_plane = g_game_gl->GetUniformLocation(program, "uClipPlane");
    zgl_int clip_on = g_game_gl->GetUniformLocation(program, "uClipOn");
    zgl_int triplanar = g_game_gl->GetUniformLocation(program, "uTriplanar");
    zgl_int height_fog_params = g_game_gl->GetUniformLocation(program, "uHeightFogParams");
    zgl_int height_fog_color = g_game_gl->GetUniformLocation(program, "uHeightFogColor");
    zgl_int hit_flash = g_game_gl->GetUniformLocation(program, "uHitFlash");
    zgl_int holo_scan_params = g_game_gl->GetUniformLocation(program, "uHoloScanParams");
    zgl_int holo_scan_color = g_game_gl->GetUniformLocation(program, "uHoloScanColor");
    zgl_int top_cover_params = g_game_gl->GetUniformLocation(program, "uTopCoverParams");
    zgl_int top_cover_color = g_game_gl->GetUniformLocation(program, "uTopCoverColor");
    zgl_int wind_params = g_game_gl->GetUniformLocation(program, "uWindParams");
    zgl_int dist_fade = g_game_gl->GetUniformLocation(program, "uDistFade");
    zgl_int world_grid_params = g_game_gl->GetUniformLocation(program, "uWorldGridParams");
    zgl_int world_grid_color = g_game_gl->GetUniformLocation(program, "uWorldGridColor");
    zgl_int caustics_params = g_game_gl->GetUniformLocation(program, "uCausticsParams");
    zgl_int caustics_color = g_game_gl->GetUniformLocation(program, "uCausticsColor");
    zgl_int ice_frost_params = g_game_gl->GetUniformLocation(program, "uIceFrostParams");
    zgl_int ice_frost_color = g_game_gl->GetUniformLocation(program, "uIceFrostColor");
    zgl_int lava_crust_params = g_game_gl->GetUniformLocation(program, "uLavaCrustParams");
    zgl_int lava_crust_color = g_game_gl->GetUniformLocation(program, "uLavaCrustColor");
    zgl_int iridescence_params = g_game_gl->GetUniformLocation(program, "uIridescenceParams");
    zgl_int sonar_origin_radius = g_game_gl->GetUniformLocation(program, "uSonarOriginRadius");
    zgl_int sonar_style = g_game_gl->GetUniformLocation(program, "uSonarStyle");
    zgl_int sonar_color = g_game_gl->GetUniformLocation(program, "uSonarColor");
    zgl_int force_field_params = g_game_gl->GetUniformLocation(program, "uForceFieldParams");
    zgl_int force_field_color = g_game_gl->GetUniformLocation(program, "uForceFieldColor");
    zgl_int rain_ripples_params = g_game_gl->GetUniformLocation(program, "uRainRipplesParams");
    zgl_int rain_ripples_color = g_game_gl->GetUniformLocation(program, "uRainRipplesColor");
    zgl_int xray_params = g_game_gl->GetUniformLocation(program, "uXrayParams");
    zgl_int xray_color = g_game_gl->GetUniformLocation(program, "uXrayColor");
    zgl_int portal_vortex_params = g_game_gl->GetUniformLocation(program, "uPortalVortexParams");
    zgl_int portal_vortex_color = g_game_gl->GetUniformLocation(program, "uPortalVortexColor");
    zgl_int blueprint_wire_params = g_game_gl->GetUniformLocation(program, "uBlueprintWireParams");
    zgl_int blueprint_wire_color = g_game_gl->GetUniformLocation(program, "uBlueprintWireColor");
    zgl_int heat_haze_params = g_game_gl->GetUniformLocation(program, "uHeatHazeParams");
    zgl_int heat_haze_color = g_game_gl->GetUniformLocation(program, "uHeatHazeColor");
    zgl_int pixel_dither_params = g_game_gl->GetUniformLocation(program, "uPixelDitherParams");
    zgl_int crosshatch_params = g_game_gl->GetUniformLocation(program, "uCrosshatchParams");
    zgl_int crosshatch_color = g_game_gl->GetUniformLocation(program, "uCrosshatchColor");
    zgl_int aniso_brushed_params = g_game_gl->GetUniformLocation(program, "uAnisoBrushedParams");
    zgl_int aniso_brushed_color = g_game_gl->GetUniformLocation(program, "uAnisoBrushedColor");
    zgl_int color = g_game_gl->GetUniformLocation(program, "uColor");
    zgl_int light_dir = g_game_gl->GetUniformLocation(program, "uLightDir[0]");
    zgl_int light_color = g_game_gl->GetUniformLocation(program, "uLightColor[0]");
    zgl_int light_count = g_game_gl->GetUniformLocation(program, "uLightCount");
    zgl_int ambient = g_game_gl->GetUniformLocation(program, "uAmbient");
    zgl_int cam_pos = g_game_gl->GetUniformLocation(program, "uCamPos");
    zgl_int cam_valid = g_game_gl->GetUniformLocation(program, "uCamValid");
    zgl_int material = g_game_gl->GetUniformLocation(program, "uMaterial");
    zgl_int emissive = g_game_gl->GetUniformLocation(program, "uEmissive");
    zgl_int point_pos = g_game_gl->GetUniformLocation(program, "uPointPos[0]");
    zgl_int point_color = g_game_gl->GetUniformLocation(program, "uPointColor[0]");
    zgl_int point_count = g_game_gl->GetUniformLocation(program, "uPointCount");
    zgl_int spot_pos = g_game_gl->GetUniformLocation(program, "uSpotPos[0]");
    zgl_int spot_dir = g_game_gl->GetUniformLocation(program, "uSpotDir[0]");
    zgl_int spot_color = g_game_gl->GetUniformLocation(program, "uSpotColor[0]");
    zgl_int spot_count = g_game_gl->GetUniformLocation(program, "uSpotCount");
    zgl_int hemi_sky = g_game_gl->GetUniformLocation(program, "uHemiSky");
    zgl_int hemi_ground = g_game_gl->GetUniformLocation(program, "uHemiGround");
    zgl_int hemi_on = g_game_gl->GetUniformLocation(program, "uHemiOn");
    zgl_int fog_color = g_game_gl->GetUniformLocation(program, "uFogColor");
    zgl_int fog_params = g_game_gl->GetUniformLocation(program, "uFogParams");
    int ok = pos == ZAN_GAME_LOC_POS && normal == ZAN_GAME_LOC_NORMAL &&
        uv == ZAN_GAME_LOC_UV && joints == ZAN_GAME_LOC_JOINTS &&
        weights == ZAN_GAME_LOC_WEIGHTS && mvp >= 0 && bones >= 0 &&
        tex >= 0 && color >= 0 && light_dir >= 0 && light_color >= 0 &&
        light_count >= 0 && ambient >= 0 && g_game_gl->GetError() == ZGL_NO_ERROR;
    if (!ok) {
        g_game_gl->DeleteProgram(program);
        return NULL;
    }
    g_game_skin_prog.program = program;
    g_game_skin_prog.u_mvp = mvp;
    g_game_skin_prog.u_model = model;
    g_game_skin_prog.u_light_vp = light_vp;
    g_game_skin_prog.u_bones = bones;
    g_game_skin_prog.u_tex = tex;
    g_game_skin_prog.u_shadow_map = shadow_map;
    g_game_skin_prog.u_shadow_on = shadow_on;
    g_game_skin_prog.u_normal_map = normal_map;
    g_game_skin_prog.u_normal_on = normal_on;
    g_game_skin_prog.u_normal_scale = normal_scale;
    g_game_skin_prog.u_orm_map = orm_map;
    g_game_skin_prog.u_orm_on = orm_on;
    g_game_skin_prog.u_ao_strength = ao_strength;
    g_game_skin_prog.u_alpha_cutoff = alpha_cutoff;
    g_game_skin_prog.u_emissive_map = emissive_map;
    g_game_skin_prog.u_emissive_on = emissive_on;
    g_game_skin_prog.u_uv_transform = uv_transform;
    g_game_skin_prog.u_rim_light = rim_light;
    g_game_skin_prog.u_toon = toon;
    g_game_skin_prog.u_dissolve = dissolve;
    g_game_skin_prog.u_dissolve_edge = dissolve_edge;
    g_game_skin_prog.u_clip_plane = clip_plane;
    g_game_skin_prog.u_clip_on = clip_on;
    g_game_skin_prog.u_triplanar = triplanar;
    g_game_skin_prog.u_height_fog_params = height_fog_params;
    g_game_skin_prog.u_height_fog_color = height_fog_color;
    g_game_skin_prog.u_hit_flash = hit_flash;
    g_game_skin_prog.u_holo_scan_params = holo_scan_params;
    g_game_skin_prog.u_holo_scan_color = holo_scan_color;
    g_game_skin_prog.u_top_cover_params = top_cover_params;
    g_game_skin_prog.u_top_cover_color = top_cover_color;
    g_game_skin_prog.u_wind_params = wind_params;
    g_game_skin_prog.u_dist_fade = dist_fade;
    g_game_skin_prog.u_world_grid_params = world_grid_params;
    g_game_skin_prog.u_world_grid_color = world_grid_color;
    g_game_skin_prog.u_caustics_params = caustics_params;
    g_game_skin_prog.u_caustics_color = caustics_color;
    g_game_skin_prog.u_ice_frost_params = ice_frost_params;
    g_game_skin_prog.u_ice_frost_color = ice_frost_color;
    g_game_skin_prog.u_lava_crust_params = lava_crust_params;
    g_game_skin_prog.u_lava_crust_color = lava_crust_color;
    g_game_skin_prog.u_iridescence_params = iridescence_params;
    g_game_skin_prog.u_sonar_origin_radius = sonar_origin_radius;
    g_game_skin_prog.u_sonar_style = sonar_style;
    g_game_skin_prog.u_sonar_color = sonar_color;
    g_game_skin_prog.u_force_field_params = force_field_params;
    g_game_skin_prog.u_force_field_color = force_field_color;
    g_game_skin_prog.u_rain_ripples_params = rain_ripples_params;
    g_game_skin_prog.u_rain_ripples_color = rain_ripples_color;
    g_game_skin_prog.u_xray_params = xray_params;
    g_game_skin_prog.u_xray_color = xray_color;
    g_game_skin_prog.u_portal_vortex_params = portal_vortex_params;
    g_game_skin_prog.u_portal_vortex_color = portal_vortex_color;
    g_game_skin_prog.u_blueprint_wire_params = blueprint_wire_params;
    g_game_skin_prog.u_blueprint_wire_color = blueprint_wire_color;
    g_game_skin_prog.u_heat_haze_params = heat_haze_params;
    g_game_skin_prog.u_heat_haze_color = heat_haze_color;
    g_game_skin_prog.u_pixel_dither_params = pixel_dither_params;
    g_game_skin_prog.u_crosshatch_params = crosshatch_params;
    g_game_skin_prog.u_crosshatch_color = crosshatch_color;
    g_game_skin_prog.u_aniso_brushed_params = aniso_brushed_params;
    g_game_skin_prog.u_aniso_brushed_color = aniso_brushed_color;
    g_game_skin_prog.u_color = color;
    g_game_skin_prog.u_light_dir = light_dir;
    g_game_skin_prog.u_light_color = light_color;
    g_game_skin_prog.u_light_count = light_count;
    g_game_skin_prog.u_ambient = ambient;
    g_game_skin_prog.u_cam_pos = cam_pos;
    g_game_skin_prog.u_cam_valid = cam_valid;
    g_game_skin_prog.u_material = material;
    g_game_skin_prog.u_emissive = emissive;
    g_game_skin_prog.u_point_pos = point_pos;
    g_game_skin_prog.u_point_color = point_color;
    g_game_skin_prog.u_point_count = point_count;
    g_game_skin_prog.u_spot_pos = spot_pos;
    g_game_skin_prog.u_spot_dir = spot_dir;
    g_game_skin_prog.u_spot_color = spot_color;
    g_game_skin_prog.u_spot_count = spot_count;
    g_game_skin_prog.u_hemi_sky = hemi_sky;
    g_game_skin_prog.u_hemi_ground = hemi_ground;
    g_game_skin_prog.u_hemi_on = hemi_on;
    g_game_skin_prog.u_fog_color = fog_color;
    g_game_skin_prog.u_fog_params = fog_params;
    g_game_skin_prog.light_version = 0;
    return &g_game_skin_prog;
}

static zan_game_shadow_program *zan_game_shadow_program_get(void) {
    if (g_game_shadow_prog.program) return &g_game_shadow_prog;
    char vs_src[1024];
    char fs_src[512];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, ZAN_GAME_3D_VS_SHADOW);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, ZAN_GAME_3D_FS_SHADOW);
    zgl_uint program = zan_gui_gpu_program(vs_src, fs_src);
    if (!program) return NULL;
    zgl_int pos = g_game_gl->GetAttribLocation(program, "a_pos");
    zgl_int light_vp = g_game_gl->GetUniformLocation(program, "uLightVP");
    zgl_int model = g_game_gl->GetUniformLocation(program, "uModel");
    zgl_int wind_params = g_game_gl->GetUniformLocation(program, "uWindParams");
    if (pos != ZAN_GAME_LOC_POS || light_vp < 0 || model < 0 ||
        g_game_gl->GetError() != ZGL_NO_ERROR) {
        g_game_gl->DeleteProgram(program);
        return NULL;
    }
    g_game_shadow_prog.program = program;
    g_game_shadow_prog.u_light_vp = light_vp;
    g_game_shadow_prog.u_model = model;
    g_game_shadow_prog.u_bones = -1;
    g_game_shadow_prog.u_wind_params = wind_params;
    return &g_game_shadow_prog;
}

static zan_game_shadow_program *zan_game_shadow_skin_program_get(void) {
    if (g_game_shadow_skin_prog.program) return &g_game_shadow_skin_prog;
    char vs_src[2048];
    char fs_src[512];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, ZAN_GAME_3D_VS_SHADOW_SKINNED);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, ZAN_GAME_3D_FS_SHADOW);
    zgl_uint program = zan_gui_gpu_program(vs_src, fs_src);
    if (!program) return NULL;
    zgl_int pos = g_game_gl->GetAttribLocation(program, "a_pos");
    zgl_int joints = g_game_gl->GetAttribLocation(program, "a_joints");
    zgl_int weights = g_game_gl->GetAttribLocation(program, "a_weights");
    zgl_int light_vp = g_game_gl->GetUniformLocation(program, "uLightVP");
    zgl_int model = g_game_gl->GetUniformLocation(program, "uModel");
    zgl_int bones = g_game_gl->GetUniformLocation(program, "uBones");
    if (pos != ZAN_GAME_LOC_POS || joints != ZAN_GAME_LOC_JOINTS ||
        weights != ZAN_GAME_LOC_WEIGHTS || light_vp < 0 || model < 0 ||
        bones < 0 || g_game_gl->GetError() != ZGL_NO_ERROR) {
        g_game_gl->DeleteProgram(program);
        return NULL;
    }
    g_game_shadow_skin_prog.program = program;
    g_game_shadow_skin_prog.u_light_vp = light_vp;
    g_game_shadow_skin_prog.u_model = model;
    g_game_shadow_skin_prog.u_bones = bones;
    return &g_game_shadow_skin_prog;
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
    if (prog->u_cam_pos >= 0) gl->Uniform3fv(prog->u_cam_pos, 1, g_game_cam_pos);
    if (prog->u_cam_valid >= 0) gl->Uniform1i(prog->u_cam_valid, g_game_cam_valid);
    if (prog->u_material >= 0) gl->Uniform2f(prog->u_material, g_game_material[0], g_game_material[1]);
    if (prog->u_emissive >= 0) gl->Uniform3fv(prog->u_emissive, 1, g_game_emissive);
    if (prog->u_point_pos >= 0 && gl->Uniform4fv) {
        gl->Uniform4fv(prog->u_point_pos, ZAN_GAME_MAX_LIGHTS, &g_game_point_pos[0][0]);
    }
    if (prog->u_point_color >= 0) {
        gl->Uniform3fv(prog->u_point_color, ZAN_GAME_MAX_LIGHTS, &g_game_point_color[0][0]);
    }
    if (prog->u_point_count >= 0) gl->Uniform1i(prog->u_point_count, g_game_point_count);
    if (prog->u_spot_pos >= 0 && gl->Uniform4fv) {
        gl->Uniform4fv(prog->u_spot_pos, ZAN_GAME_MAX_LIGHTS, &g_game_spot_pos[0][0]);
    }
    if (prog->u_spot_dir >= 0 && gl->Uniform4fv) {
        gl->Uniform4fv(prog->u_spot_dir, ZAN_GAME_MAX_LIGHTS, &g_game_spot_dir[0][0]);
    }
    if (prog->u_spot_color >= 0 && gl->Uniform4fv) {
        gl->Uniform4fv(prog->u_spot_color, ZAN_GAME_MAX_LIGHTS, &g_game_spot_color[0][0]);
    }
    if (prog->u_spot_count >= 0) gl->Uniform1i(prog->u_spot_count, g_game_spot_count);
    if (prog->u_hemi_sky >= 0) gl->Uniform3fv(prog->u_hemi_sky, 1, g_game_hemi_sky);
    if (prog->u_hemi_ground >= 0) gl->Uniform3fv(prog->u_hemi_ground, 1, g_game_hemi_ground);
    if (prog->u_hemi_on >= 0) gl->Uniform1i(prog->u_hemi_on, g_game_hemi_on);
    if (prog->u_fog_color >= 0) gl->Uniform3fv(prog->u_fog_color, 1, g_game_fog_color);
    if (prog->u_fog_params >= 0) gl->Uniform3fv(prog->u_fog_params, 1, g_game_fog_params);
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
 * flags bit1 = skinned：顶点 16 float（pos3+normal3+uv2+joints4+weights4），
 * 否则 8 float。返回网格 id，0 = 失败/后端不支持。 */
ZAN_GAME_API int32_t zan_game_mesh_create_ex(int32_t surface_id,
    const float *verts, int32_t count, const unsigned short *indices,
    int32_t index_count, int32_t flags) {
    int skinned = (flags & 2) != 0;
    int colored = !skinned && (flags & 1) != 0;
    int vertex_floats = skinned ? ZAN_GAME_SKINNED_VERTEX_FLOATS
        : (colored ? ZAN_GAME_COLORED_VERTEX_FLOATS : ZAN_GAME_VERTEX_FLOATS);
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
    if (!mesh) goto done;
    if (skinned) {
        if (!zan_game_skin_program_get()) goto done;
    } else {
        if (!zan_game_program_get(colored, 0)) goto done;
    }

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
    } else if (skinned) {
        gl->EnableVertexAttribArray(ZAN_GAME_LOC_JOINTS);
        gl->VertexAttribPointer(ZAN_GAME_LOC_JOINTS, 4, ZGL_FLOAT, ZGL_FALSE,
            vertex_floats * (zgl_sizei)sizeof(float),
            (const void *)(8 * sizeof(float)));
        gl->EnableVertexAttribArray(ZAN_GAME_LOC_WEIGHTS);
        gl->VertexAttribPointer(ZAN_GAME_LOC_WEIGHTS, 4, ZGL_FLOAT, ZGL_FALSE,
            vertex_floats * (zgl_sizei)sizeof(float),
            (const void *)(12 * sizeof(float)));
    }
    if (gl->GetError() != ZGL_NO_ERROR) goto failed;
    mesh->index_count = index_count;
    mesh->colored = colored;
    mesh->skinned = skinned;
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
        /* 优先 24-bit 深度缓冲抑制远景 Z-fighting；不支持时降级 16-bit。 */
        gl->RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH_COMPONENT24,
                                frame->width, frame->height);
        if (gl->GetError() != ZGL_NO_ERROR) {
            gl->RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH_COMPONENT16,
                                    frame->width, frame->height);
        }
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

#define ZAN_GAME_GL_FRONT 0x0404

static void zan_game_apply_blend_state(const zan_gl_api *gl) {
    if (g_game_blend_enabled == 2) {
        gl->Enable(ZGL_BLEND);
        if (gl->BlendFuncSeparate) {
            gl->BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE,
                                  ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        } else {
            gl->BlendFunc(ZGL_SRC_ALPHA, ZGL_ONE);
        }
    } else if (g_game_blend_enabled == 1) {
        gl->Enable(ZGL_BLEND);
        if (gl->BlendFuncSeparate) {
            gl->BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                  ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        } else {
            gl->BlendFunc(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA);
        }
    } else {
        gl->Disable(ZGL_BLEND);
    }
    if (gl->DepthMask) {
        gl->DepthMask(g_game_depth_write ? ZGL_TRUE : ZGL_FALSE);
    }
    if (g_game_cull_mode != 0 && gl->CullFace) {
        gl->Enable(ZGL_CULL_FACE);
        /* clip 不翻 Y，标准 CCW 即正面 */
        if (gl->FrontFace) gl->FrontFace(ZGL_CCW);
        gl->CullFace(g_game_cull_mode == 2 ? ZAN_GAME_GL_FRONT : ZGL_BACK);
    } else {
        gl->Disable(ZGL_CULL_FACE);
        if (gl->FrontFace) gl->FrontFace(ZGL_CCW);
    }
}

static void zan_game_restore_cull_state(const zan_gl_api *gl) {
    gl->Disable(ZGL_CULL_FACE);
    if (gl->FrontFace) gl->FrontFace(ZGL_CCW);
}

static void zan_game_bind_shadow_uniforms(const zan_gl_api *gl, zan_game_program *prog,
                                          const float *model) {
    if (prog->u_model >= 0) {
        gl->UniformMatrix4fv(prog->u_model, 1, ZGL_FALSE,
            model ? model : g_game_identity_mat4);
    }
    if (prog->u_light_vp >= 0) {
        gl->UniformMatrix4fv(prog->u_light_vp, 1, ZGL_FALSE, g_game_shadow_light_vp);
    }
    int use_shadow = g_game_shadow_ready && g_game_shadow_tex && model != NULL;
    if (prog->u_shadow_on >= 0) {
        gl->Uniform1i(prog->u_shadow_on, use_shadow ? 1 : 0);
    }
    if (prog->u_shadow_map >= 0) {
        gl->ActiveTexture(ZGL_TEXTURE1);
        gl->BindTexture(ZGL_TEXTURE_2D, use_shadow ? g_game_shadow_tex : 0);
        gl->Uniform1i(prog->u_shadow_map, 1);
    }
    zgl_uint ntex = 0;
    if (g_game_normal_key[0] && g_game_normal_scale > 0.0001f) {
        const zan_bitmap *nbmp = zan_image_get(g_game_normal_key);
        if (zan_game_bitmap_valid(nbmp)) {
            ntex = zan_gui_gpu_texture(nbmp);
        }
    }
    if (prog->u_normal_on >= 0) {
        gl->Uniform1i(prog->u_normal_on, ntex ? 1 : 0);
    }
    if (prog->u_normal_scale >= 0 && gl->Uniform1f) {
        gl->Uniform1f(prog->u_normal_scale, ntex ? g_game_normal_scale : 0.0f);
    }
    if (prog->u_normal_map >= 0) {
        gl->ActiveTexture(ZGL_TEXTURE0 + 2);
        gl->BindTexture(ZGL_TEXTURE_2D, ntex);
        gl->Uniform1i(prog->u_normal_map, 2);
    }
    zgl_uint orm_tex = 0;
    if (g_game_orm_key[0]) {
        const zan_bitmap *obmp = zan_image_get(g_game_orm_key);
        if (zan_game_bitmap_valid(obmp)) {
            orm_tex = zan_gui_gpu_texture(obmp);
        }
    }
    if (prog->u_orm_on >= 0) {
        gl->Uniform1i(prog->u_orm_on, orm_tex ? 1 : 0);
    }
    if (prog->u_ao_strength >= 0 && gl->Uniform1f) {
        gl->Uniform1f(prog->u_ao_strength, orm_tex ? g_game_ao_strength : 0.0f);
    }
    if (prog->u_orm_map >= 0) {
        gl->ActiveTexture(ZGL_TEXTURE0 + 3);
        gl->BindTexture(ZGL_TEXTURE_2D, orm_tex);
        gl->Uniform1i(prog->u_orm_map, 3);
    }
    if (prog->u_alpha_cutoff >= 0 && gl->Uniform1f) {
        gl->Uniform1f(prog->u_alpha_cutoff, g_game_alpha_cutoff);
    }
    zgl_uint em_tex = 0;
    if (g_game_emissive_key[0]) {
        const zan_bitmap *ebmp = zan_image_get(g_game_emissive_key);
        if (zan_game_bitmap_valid(ebmp)) {
            em_tex = zan_gui_gpu_texture(ebmp);
        }
    }
    if (prog->u_emissive_on >= 0) {
        gl->Uniform1i(prog->u_emissive_on, em_tex ? 1 : 0);
    }
    if (prog->u_emissive_map >= 0) {
        gl->ActiveTexture(ZGL_TEXTURE0 + 4);
        gl->BindTexture(ZGL_TEXTURE_2D, em_tex);
        gl->Uniform1i(prog->u_emissive_map, 4);
    }
    if (prog->u_uv_transform >= 0) {
        gl->Uniform4f(prog->u_uv_transform,
            g_game_uv_transform[0], g_game_uv_transform[1],
            g_game_uv_transform[2], g_game_uv_transform[3]);
    }
    if (prog->u_rim_light >= 0) {
        gl->Uniform4f(prog->u_rim_light,
            g_game_rim_light[0], g_game_rim_light[1],
            g_game_rim_light[2], g_game_rim_light[3]);
    }
    if (prog->u_toon >= 0) {
        gl->Uniform2f(prog->u_toon, g_game_toon[0], g_game_toon[1]);
    }
    if (prog->u_dissolve >= 0) {
        gl->Uniform2f(prog->u_dissolve, g_game_dissolve[0], g_game_dissolve[1]);
    }
    if (prog->u_dissolve_edge >= 0) {
        gl->Uniform3fv(prog->u_dissolve_edge, 1, g_game_dissolve_edge);
    }
    if (prog->u_clip_plane >= 0) {
        gl->Uniform4f(prog->u_clip_plane,
            g_game_clip_plane[0], g_game_clip_plane[1],
            g_game_clip_plane[2], g_game_clip_plane[3]);
    }
    if (prog->u_clip_on >= 0) {
        gl->Uniform1i(prog->u_clip_on, g_game_clip_on);
    }
    if (prog->u_triplanar >= 0) {
        gl->Uniform3fv(prog->u_triplanar, 1, g_game_triplanar);
    }
    if (prog->u_height_fog_params >= 0) {
        gl->Uniform4f(prog->u_height_fog_params,
            g_game_height_fog_params[0], g_game_height_fog_params[1],
            g_game_height_fog_params[2], g_game_height_fog_params[3]);
    }
    if (prog->u_height_fog_color >= 0) {
        gl->Uniform3fv(prog->u_height_fog_color, 1, g_game_height_fog_color);
    }
    if (prog->u_hit_flash >= 0) {
        gl->Uniform4f(prog->u_hit_flash,
            g_game_hit_flash[0], g_game_hit_flash[1],
            g_game_hit_flash[2], g_game_hit_flash[3]);
    }
    if (prog->u_holo_scan_params >= 0) {
        gl->Uniform4f(prog->u_holo_scan_params,
            g_game_holo_scan_params[0], g_game_holo_scan_params[1],
            g_game_holo_scan_params[2], g_game_holo_scan_params[3]);
    }
    if (prog->u_holo_scan_color >= 0) {
        gl->Uniform3fv(prog->u_holo_scan_color, 1, g_game_holo_scan_color);
    }
    if (prog->u_top_cover_params >= 0) {
        gl->Uniform4f(prog->u_top_cover_params,
            g_game_top_cover_params[0], g_game_top_cover_params[1],
            g_game_top_cover_params[2], g_game_top_cover_params[3]);
    }
    if (prog->u_top_cover_color >= 0) {
        gl->Uniform3fv(prog->u_top_cover_color, 1, g_game_top_cover_color);
    }
    if (prog->u_wind_params >= 0) {
        gl->Uniform4f(prog->u_wind_params,
            g_game_wind_params[0], g_game_wind_params[1],
            g_game_wind_params[2], g_game_wind_params[3]);
    }
    if (prog->u_dist_fade >= 0) {
        gl->Uniform3fv(prog->u_dist_fade, 1, g_game_dist_fade);
    }
    if (prog->u_world_grid_params >= 0) {
        gl->Uniform4f(prog->u_world_grid_params,
            g_game_world_grid_params[0], g_game_world_grid_params[1],
            g_game_world_grid_params[2], g_game_world_grid_params[3]);
    }
    if (prog->u_world_grid_color >= 0) {
        gl->Uniform3fv(prog->u_world_grid_color, 1, g_game_world_grid_color);
    }
    if (prog->u_caustics_params >= 0) {
        gl->Uniform4f(prog->u_caustics_params,
            g_game_caustics_params[0], g_game_caustics_params[1],
            g_game_caustics_params[2], g_game_caustics_params[3]);
    }
    if (prog->u_caustics_color >= 0) {
        gl->Uniform3fv(prog->u_caustics_color, 1, g_game_caustics_color);
    }
    if (prog->u_ice_frost_params >= 0) {
        gl->Uniform4f(prog->u_ice_frost_params,
            g_game_ice_frost_params[0], g_game_ice_frost_params[1],
            g_game_ice_frost_params[2], g_game_ice_frost_params[3]);
    }
    if (prog->u_ice_frost_color >= 0) {
        gl->Uniform3fv(prog->u_ice_frost_color, 1, g_game_ice_frost_color);
    }
    if (prog->u_lava_crust_params >= 0) {
        gl->Uniform4f(prog->u_lava_crust_params,
            g_game_lava_crust_params[0], g_game_lava_crust_params[1],
            g_game_lava_crust_params[2], g_game_lava_crust_params[3]);
    }
    if (prog->u_lava_crust_color >= 0) {
        gl->Uniform4f(prog->u_lava_crust_color,
            g_game_lava_crust_color[0], g_game_lava_crust_color[1],
            g_game_lava_crust_color[2], g_game_lava_crust_color[3]);
    }
    if (prog->u_iridescence_params >= 0) {
        gl->Uniform4f(prog->u_iridescence_params,
            g_game_iridescence_params[0], g_game_iridescence_params[1],
            g_game_iridescence_params[2], g_game_iridescence_params[3]);
    }
    if (prog->u_sonar_origin_radius >= 0) {
        gl->Uniform4f(prog->u_sonar_origin_radius,
            g_game_sonar_origin_radius[0], g_game_sonar_origin_radius[1],
            g_game_sonar_origin_radius[2], g_game_sonar_origin_radius[3]);
    }
    if (prog->u_sonar_style >= 0) {
        gl->Uniform3fv(prog->u_sonar_style, 1, g_game_sonar_style);
    }
    if (prog->u_sonar_color >= 0) {
        gl->Uniform3fv(prog->u_sonar_color, 1, g_game_sonar_color);
    }
    if (prog->u_force_field_params >= 0) {
        gl->Uniform4f(prog->u_force_field_params,
            g_game_force_field_params[0], g_game_force_field_params[1],
            g_game_force_field_params[2], g_game_force_field_params[3]);
    }
    if (prog->u_force_field_color >= 0) {
        gl->Uniform4f(prog->u_force_field_color,
            g_game_force_field_color[0], g_game_force_field_color[1],
            g_game_force_field_color[2], g_game_force_field_color[3]);
    }
    if (prog->u_rain_ripples_params >= 0) {
        gl->Uniform4f(prog->u_rain_ripples_params,
            g_game_rain_ripples_params[0], g_game_rain_ripples_params[1],
            g_game_rain_ripples_params[2], g_game_rain_ripples_params[3]);
    }
    if (prog->u_rain_ripples_color >= 0) {
        gl->Uniform3fv(prog->u_rain_ripples_color, 1, g_game_rain_ripples_color);
    }
    if (prog->u_xray_params >= 0) {
        gl->Uniform3fv(prog->u_xray_params, 1, g_game_xray_params);
    }
    if (prog->u_xray_color >= 0) {
        gl->Uniform3fv(prog->u_xray_color, 1, g_game_xray_color);
    }
    if (prog->u_portal_vortex_params >= 0) {
        gl->Uniform4f(prog->u_portal_vortex_params,
            g_game_portal_vortex_params[0], g_game_portal_vortex_params[1],
            g_game_portal_vortex_params[2], g_game_portal_vortex_params[3]);
    }
    if (prog->u_portal_vortex_color >= 0) {
        gl->Uniform3fv(prog->u_portal_vortex_color, 1, g_game_portal_vortex_color);
    }
    if (prog->u_blueprint_wire_params >= 0) {
        gl->Uniform4f(prog->u_blueprint_wire_params,
            g_game_blueprint_wire_params[0], g_game_blueprint_wire_params[1],
            g_game_blueprint_wire_params[2], g_game_blueprint_wire_params[3]);
    }
    if (prog->u_blueprint_wire_color >= 0) {
        gl->Uniform3fv(prog->u_blueprint_wire_color, 1, g_game_blueprint_wire_color);
    }
    if (prog->u_heat_haze_params >= 0) {
        gl->Uniform4f(prog->u_heat_haze_params,
            g_game_heat_haze_params[0], g_game_heat_haze_params[1],
            g_game_heat_haze_params[2], g_game_heat_haze_params[3]);
    }
    if (prog->u_heat_haze_color >= 0) {
        gl->Uniform3fv(prog->u_heat_haze_color, 1, g_game_heat_haze_color);
    }
    if (prog->u_pixel_dither_params >= 0) {
        gl->Uniform3fv(prog->u_pixel_dither_params, 1, g_game_pixel_dither_params);
    }
    if (prog->u_crosshatch_params >= 0) {
        gl->Uniform3fv(prog->u_crosshatch_params, 1, g_game_crosshatch_params);
    }
    if (prog->u_crosshatch_color >= 0) {
        gl->Uniform3fv(prog->u_crosshatch_color, 1, g_game_crosshatch_color);
    }
    if (prog->u_aniso_brushed_params >= 0) {
        gl->Uniform4f(prog->u_aniso_brushed_params,
            g_game_aniso_brushed_params[0], g_game_aniso_brushed_params[1],
            g_game_aniso_brushed_params[2], g_game_aniso_brushed_params[3]);
    }
    if (prog->u_aniso_brushed_color >= 0) {
        gl->Uniform3fv(prog->u_aniso_brushed_color, 1, g_game_aniso_brushed_color);
    }
}

ZAN_GAME_API int32_t zan_game_draw3d_ex(int32_t surface_id, int32_t mesh_id,
    const float *mvp, const float *model, int32_t color, const char *texture) {
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
    if (!g_game_blend_enabled && !g_game_preserve_depth) {
        if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
        gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    }
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    zan_game_apply_blend_state(gl);
    gl->UseProgram(prog->program);
    zan_game_upload_lights(prog);
    gl->UniformMatrix4fv(prog->u_mvp, 1, ZGL_FALSE, mvp);
    zan_game_bind_shadow_uniforms(gl, prog, model);
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
    zan_game_restore_cull_state(gl);
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}

ZAN_GAME_API int32_t zan_game_draw3d(int32_t surface_id, int32_t mesh_id,
    const float *mvp, int32_t color, const char *texture) {
    return zan_game_draw3d_ex(surface_id, mesh_id, mvp, NULL, color, texture);
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
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    zan_game_apply_blend_state(gl);
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
    if (g_game_gl) {
        zan_game_restore_cull_state(g_game_gl);
        if (g_game_gl->DepthMask) g_game_gl->DepthMask(ZGL_TRUE);
        g_game_gl->Finish();
    }
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

ZAN_GAME_API int32_t zan_game_set_blend3d(int32_t enabled, int32_t depth_write) {
    g_game_blend_enabled = (enabled == 2) ? 2 : (enabled != 0 ? 1 : 0);
    g_game_depth_write = depth_write != 0 ? 1 : 0;
    if (g_game_batch_surface >= 0 && g_game_gl) {
        zan_game_apply_blend_state(g_game_gl);
    }
    return 1;
}

ZAN_GAME_API int32_t zan_game_set_cull3d(int32_t mode) {
    if (mode < 0 || mode > 2) return 0;
    g_game_cull_mode = mode;
    if (g_game_batch_surface >= 0 && g_game_gl) {
        zan_game_apply_blend_state(g_game_gl);
    }
    return 1;
}

ZAN_GAME_API int32_t zan_game_begin_frame3d(int32_t surface_id) {
    if (surface_id < 0 || surface_id >= ZAN_GAME_SURFACE_CAP) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    const zan_gl_api *gl = frame.api;
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    if (!zan_game_depth_ready(surface_id, &frame)) goto done;
    gl->Disable(ZGL_SCISSOR_TEST);
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    g_game_preserve_depth = 1;
    result = gl->GetError() == ZGL_NO_ERROR;
done:
    zan_gui_gpu_end(surface_id);
    return result;
}

ZAN_GAME_API void zan_game_end_frame3d(void) {
    g_game_preserve_depth = 0;
}

static int zan_game_shadow_fbo_ready(const zan_gl_api *gl, int map_size) {
    if (g_game_shadow_fbo && g_game_shadow_size == map_size) return 1;
    zan_game_drop_shadow();
    gl->GenTextures(1, &g_game_shadow_tex);
    if (!g_game_shadow_tex) return 0;
    gl->BindTexture(ZGL_TEXTURE_2D, g_game_shadow_tex);
    gl->TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_DEPTH_COMPONENT24, map_size, map_size,
                   0, ZGL_DEPTH_COMPONENT, ZGL_UNSIGNED_INT, NULL);
    if (gl->GetError() != ZGL_NO_ERROR) {
        gl->TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_DEPTH_COMPONENT16, map_size, map_size,
                       0, ZGL_DEPTH_COMPONENT, ZGL_UNSIGNED_SHORT, NULL);
    }
    gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
    gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
    gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl->GenFramebuffers(1, &g_game_shadow_fbo);
    if (!g_game_shadow_fbo) { zan_game_drop_shadow(); return 0; }
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, g_game_shadow_fbo);
    gl->FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_DEPTH_ATTACHMENT,
                             ZGL_TEXTURE_2D, g_game_shadow_tex, 0);
    if (gl->CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE ||
        gl->GetError() != ZGL_NO_ERROR) {
        zan_game_drop_shadow();
        return 0;
    }
    g_game_shadow_size = map_size;
    return 1;
}

ZAN_GAME_API int32_t zan_game_shadow_begin(int32_t surface_id, int32_t map_size,
    const float *light_vp) {
    if (surface_id < 0 || surface_id >= ZAN_GAME_SURFACE_CAP || !light_vp ||
        g_game_shadow_surface != -1 || g_game_batch_surface != -1) return 0;
    if (map_size < ZAN_GAME_SHADOW_MIN_SIZE) map_size = ZAN_GAME_SHADOW_MIN_SIZE;
    if (map_size > ZAN_GAME_SHADOW_MAX_SIZE) map_size = ZAN_GAME_SHADOW_MAX_SIZE;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    if (!zan_game_gpu_prepare(&frame) || !zan_game_shadow_program_get()) {
        zan_gui_gpu_end(surface_id);
        return 0;
    }
    const zan_gl_api *gl = frame.api;
    if (!zan_game_shadow_fbo_ready(gl, map_size)) {
        zan_gui_gpu_end(surface_id);
        return 0;
    }
    memcpy(g_game_shadow_light_vp, light_vp, 16 * sizeof(float));
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, g_game_shadow_fbo);
    gl->Viewport(0, 0, map_size, map_size);
    gl->Disable(ZGL_SCISSOR_TEST);
    if (gl->ColorMask) gl->ColorMask(ZGL_FALSE, ZGL_FALSE, ZGL_FALSE, ZGL_FALSE);
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    gl->Disable(ZGL_BLEND);
    if (gl->PolygonOffset) {
        gl->Enable(ZGL_POLYGON_OFFSET_FILL);
        gl->PolygonOffset(2.0f, 4.0f);
    }
    gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    g_game_shadow_frame = frame;
    g_game_shadow_surface = surface_id;
    g_game_shadow_ready = 0;
    return 1;
}

ZAN_GAME_API int32_t zan_game_shadow_draw(int32_t surface_id, int32_t mesh_id,
    const float *model) {
    if (surface_id < 0 || surface_id != g_game_shadow_surface ||
        mesh_id <= 0 || !model) return 0;
    zan_gpu_frame *frame = &g_game_shadow_frame;
    if (!zan_game_gpu_prepare(frame)) return 0;
    zan_game_mesh *mesh = zan_game_find_mesh(mesh_id, frame->epoch);
    if (!mesh) return 0;
    zan_game_shadow_program *prog = zan_game_shadow_program_get();
    if (!prog) return 0;
    const zan_gl_api *gl = frame->api;
    gl->UseProgram(prog->program);
    gl->UniformMatrix4fv(prog->u_light_vp, 1, ZGL_FALSE, g_game_shadow_light_vp);
    gl->UniformMatrix4fv(prog->u_model, 1, ZGL_FALSE, model);
    if (prog->u_wind_params >= 0) {
        gl->Uniform4f(prog->u_wind_params,
            g_game_wind_params[0], g_game_wind_params[1],
            g_game_wind_params[2], g_game_wind_params[3]);
    }
    gl->BindVertexArray(mesh->vao);
    gl->DrawElements(ZGL_TRIANGLES, mesh->index_count, ZGL_UNSIGNED_SHORT, NULL);
    gl->BindVertexArray(0);
    return gl->GetError() == ZGL_NO_ERROR;
}

ZAN_GAME_API int32_t zan_game_shadow_draw_skinned(int32_t surface_id, int32_t mesh_id,
    const float *model) {
    if (surface_id < 0 || surface_id != g_game_shadow_surface ||
        mesh_id <= 0 || !model) return 0;
    zan_gpu_frame *frame = &g_game_shadow_frame;
    if (!zan_game_gpu_prepare(frame)) return 0;
    zan_game_mesh *mesh = zan_game_find_mesh(mesh_id, frame->epoch);
    if (!mesh || !mesh->skinned) return 0;
    zan_game_shadow_program *prog = zan_game_shadow_skin_program_get();
    if (!prog) return 0;
    const zan_gl_api *gl = frame->api;
    if (g_game_bone_count == 0) {
        for (int i = 0; i < ZAN_GAME_MAX_BONES; ++i) {
            memcpy(&g_game_bones[i * 16], g_game_identity_mat4, 16 * sizeof(float));
        }
    }
    gl->UseProgram(prog->program);
    gl->UniformMatrix4fv(prog->u_light_vp, 1, ZGL_FALSE, g_game_shadow_light_vp);
    gl->UniformMatrix4fv(prog->u_model, 1, ZGL_FALSE, model);
    gl->UniformMatrix4fv(prog->u_bones, ZAN_GAME_MAX_BONES, ZGL_FALSE, g_game_bones);
    gl->BindVertexArray(mesh->vao);
    gl->DrawElements(ZGL_TRIANGLES, mesh->index_count, ZGL_UNSIGNED_SHORT, NULL);
    gl->BindVertexArray(0);
    return gl->GetError() == ZGL_NO_ERROR;
}

ZAN_GAME_API int32_t zan_game_shadow_end(int32_t surface_id) {
    if (surface_id < 0 || surface_id != g_game_shadow_surface) return 0;
    const zan_gl_api *gl = g_game_shadow_frame.api;
    if (gl) {
        if (gl->PolygonOffset) gl->Disable(ZGL_POLYGON_OFFSET_FILL);
        if (gl->ColorMask) gl->ColorMask(ZGL_TRUE, ZGL_TRUE, ZGL_TRUE, ZGL_TRUE);
        gl->BindFramebuffer(ZGL_FRAMEBUFFER, g_game_shadow_frame.framebuffer);
        gl->Finish();
    }
    g_game_shadow_surface = -1;
    g_game_shadow_ready = 1;
    zan_gui_gpu_end(surface_id);
    return 1;
}

ZAN_GAME_API void zan_game_shadow_clear(void) {
    g_game_shadow_ready = 0;
}

ZAN_GAME_API void zan_game_set_camera3d(float x, float y, float z) {
    g_game_cam_pos[0] = x;
    g_game_cam_pos[1] = y;
    g_game_cam_pos[2] = z;
    g_game_cam_valid = 1;
    g_game_light_version++;
}

ZAN_GAME_API void zan_game_set_material3d(float metallic, float roughness,
    float em_r, float em_g, float em_b) {
    if (metallic < 0.0f) metallic = 0.0f;
    if (metallic > 1.0f) metallic = 1.0f;
    if (roughness < 0.05f) roughness = 0.05f;
    if (roughness > 1.0f) roughness = 1.0f;
    g_game_material[0] = metallic;
    g_game_material[1] = roughness;
    g_game_emissive[0] = em_r;
    g_game_emissive[1] = em_g;
    g_game_emissive[2] = em_b;
    g_game_light_version++;
}

ZAN_GAME_API int32_t zan_game_set_point_lights3d(int32_t count,
    const float *pos_range4, const float *colors3) {
    if (count < 0 || count > ZAN_GAME_MAX_LIGHTS) return 0;
    if (count > 0 && (!pos_range4 || !colors3)) return 0;
    for (int i = 0; i < count; ++i) {
        g_game_point_pos[i][0] = pos_range4[i * 4 + 0];
        g_game_point_pos[i][1] = pos_range4[i * 4 + 1];
        g_game_point_pos[i][2] = pos_range4[i * 4 + 2];
        g_game_point_pos[i][3] = pos_range4[i * 4 + 3];
        g_game_point_color[i][0] = colors3[i * 3 + 0];
        g_game_point_color[i][1] = colors3[i * 3 + 1];
        g_game_point_color[i][2] = colors3[i * 3 + 2];
    }
    for (int i = count; i < ZAN_GAME_MAX_LIGHTS; ++i) {
        g_game_point_pos[i][0] = g_game_point_pos[i][1] =
            g_game_point_pos[i][2] = g_game_point_pos[i][3] = 0.0f;
        g_game_point_color[i][0] = g_game_point_color[i][1] =
            g_game_point_color[i][2] = 0.0f;
    }
    g_game_point_count = count;
    g_game_light_version++;
    return 1;
}

ZAN_GAME_API void zan_game_set_fog3d(int32_t enabled, float r, float g, float b,
    float start_dist, float end_dist) {
    g_game_fog_color[0] = r;
    g_game_fog_color[1] = g;
    g_game_fog_color[2] = b;
    float span = end_dist - start_dist;
    if (span < 0.0001f) span = 0.0001f;
    g_game_fog_params[0] = enabled ? 1.0f : 0.0f;
    g_game_fog_params[1] = start_dist;
    g_game_fog_params[2] = 1.0f / span;
    g_game_light_version++;
}

ZAN_GAME_API int32_t zan_game_set_bones3d(int32_t count, const float *matrices16) {
    if (count < 0 || count > ZAN_GAME_MAX_BONES) return 0;
    if (count > 0 && !matrices16) return 0;
    if (count > 0) {
        memcpy(g_game_bones, matrices16, (size_t)count * 16 * sizeof(float));
    }
    for (int i = count; i < ZAN_GAME_MAX_BONES; ++i) {
        memcpy(&g_game_bones[i * 16], g_game_identity_mat4, 16 * sizeof(float));
    }
    g_game_bone_count = count;
    return 1;
}

ZAN_GAME_API int32_t zan_game_draw3d_skinned(int32_t surface_id, int32_t mesh_id,
    const float *mvp, const float *model, int32_t color, const char *texture) {
    if (!mvp || mesh_id <= 0 || surface_id < 0 ||
        surface_id >= ZAN_GAME_SURFACE_CAP) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_mesh *mesh = zan_game_find_mesh(mesh_id, frame.epoch);
    if (!mesh || !mesh->skinned) goto done;
    zan_game_program *prog = zan_game_skin_program_get();
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
    if (g_game_bone_count == 0) {
        for (int i = 0; i < ZAN_GAME_MAX_BONES; ++i) {
            memcpy(&g_game_bones[i * 16], g_game_identity_mat4, 16 * sizeof(float));
        }
    }
    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    if (!zan_game_depth_ready(surface_id, &frame)) goto done;
    gl->Viewport(0, 0, frame.width, frame.height);
    gl->Enable(ZGL_SCISSOR_TEST);
    gl->Scissor(clip_x, clip_y, clip_w, clip_h);
    if (!g_game_blend_enabled && !g_game_preserve_depth) {
        if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
        gl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl->Clear(ZGL_DEPTH_BUFFER_BIT);
    }
    gl->Enable(ZGL_DEPTH_TEST);
    gl->DepthFunc(ZGL_LEQUAL);
    zan_game_apply_blend_state(gl);
    gl->UseProgram(prog->program);
    zan_game_upload_lights(prog);
    gl->UniformMatrix4fv(prog->u_mvp, 1, ZGL_FALSE, mvp);
    gl->UniformMatrix4fv(prog->u_bones, ZAN_GAME_MAX_BONES, ZGL_FALSE, g_game_bones);
    zan_game_bind_shadow_uniforms(gl, prog, model);
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
    zan_game_restore_cull_state(gl);
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}

static zan_game_post_program *zan_game_post_program_get(void) {
    if (g_game_post_prog.program) return &g_game_post_prog;
    char vs_src[1024];
    char fs_src[32768];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, ZAN_GAME_POST_VS);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, ZAN_GAME_POST_FS);
    zgl_uint prog = zan_gui_gpu_program(vs_src, fs_src);
    if (!prog) return NULL;
    g_game_post_prog.program = prog;
    g_game_post_prog.u_scene = g_game_gl->GetUniformLocation(prog, "uScene");
    g_game_post_prog.u_texel_size = g_game_gl->GetUniformLocation(prog, "uTexelSize");
    g_game_post_prog.u_exposure = g_game_gl->GetUniformLocation(prog, "uExposure");
    g_game_post_prog.u_tonemap = g_game_gl->GetUniformLocation(prog, "uTonemap");
    g_game_post_prog.u_vignette = g_game_gl->GetUniformLocation(prog, "uVignette");
    g_game_post_prog.u_bloom = g_game_gl->GetUniformLocation(prog, "uBloom");
    g_game_post_prog.u_color_grade = g_game_gl->GetUniformLocation(prog, "uColorGrade");
    g_game_post_prog.u_color_tint = g_game_gl->GetUniformLocation(prog, "uColorTint");
    g_game_post_prog.u_post_lens = g_game_gl->GetUniformLocation(prog, "uPostLens");
    g_game_post_prog.u_post_film = g_game_gl->GetUniformLocation(prog, "uPostFilm");
    g_game_post_prog.u_post_cinema = g_game_gl->GetUniformLocation(prog, "uPostCinema");
    g_game_post_prog.u_post_radial_blur = g_game_gl->GetUniformLocation(prog, "uPostRadialBlur");
    g_game_post_prog.u_post_pixelate = g_game_gl->GetUniformLocation(prog, "uPostPixelate");
    g_game_post_prog.u_post_duotone_dark = g_game_gl->GetUniformLocation(prog, "uPostDuotoneDark");
    g_game_post_prog.u_post_duotone_light = g_game_gl->GetUniformLocation(prog, "uPostDuotoneLight");
    g_game_post_prog.u_post_crt = g_game_gl->GetUniformLocation(prog, "uPostCrt");
    g_game_post_prog.u_post_outline_params = g_game_gl->GetUniformLocation(prog, "uPostOutlineParams");
    g_game_post_prog.u_post_outline_color = g_game_gl->GetUniformLocation(prog, "uPostOutlineColor");
    g_game_post_prog.u_post_thermal = g_game_gl->GetUniformLocation(prog, "uPostThermal");
    g_game_post_prog.u_post_flash_params = g_game_gl->GetUniformLocation(prog, "uPostFlashParams");
    g_game_post_prog.u_post_flash_color = g_game_gl->GetUniformLocation(prog, "uPostFlashColor");
    g_game_post_prog.u_post_night_vision = g_game_gl->GetUniformLocation(prog, "uPostNightVision");
    g_game_post_prog.u_post_halftone = g_game_gl->GetUniformLocation(prog, "uPostHalftone");
    g_game_post_prog.u_post_glitch = g_game_gl->GetUniformLocation(prog, "uPostGlitch");
    g_game_post_prog.u_post_tilt_shift = g_game_gl->GetUniformLocation(prog, "uPostTiltShift");
    g_game_post_prog.u_post_frost_params = g_game_gl->GetUniformLocation(prog, "uPostFrostParams");
    g_game_post_prog.u_post_frost_color = g_game_gl->GetUniformLocation(prog, "uPostFrostColor");
    g_game_post_prog.u_post_sepia = g_game_gl->GetUniformLocation(prog, "uPostSepia");
    g_game_post_prog.u_post_streak_params = g_game_gl->GetUniformLocation(prog, "uPostStreakParams");
    g_game_post_prog.u_post_streak_color = g_game_gl->GetUniformLocation(prog, "uPostStreakColor");
    g_game_post_prog.u_post_split_params = g_game_gl->GetUniformLocation(prog, "uPostSplitParams");
    g_game_post_prog.u_post_split_shadow = g_game_gl->GetUniformLocation(prog, "uPostSplitShadow");
    g_game_post_prog.u_post_split_high = g_game_gl->GetUniformLocation(prog, "uPostSplitHigh");
    g_game_post_prog.u_post_oil_paint = g_game_gl->GetUniformLocation(prog, "uPostOilPaint");
    g_game_post_prog.u_post_scope_blur = g_game_gl->GetUniformLocation(prog, "uPostScopeBlur");
    g_game_post_prog.u_post_daltonize = g_game_gl->GetUniformLocation(prog, "uPostDaltonize");
    return &g_game_post_prog;
}

static int zan_game_post_target_ready(const zan_gpu_frame *frame) {
    const zan_gl_api *gl = frame->api;
    if (!gl->BlitFramebuffer) return 0;
    if (g_game_post_fbo && (g_game_post_w != frame->width || g_game_post_h != frame->height)) {
        if (g_game_post_fbo) gl->DeleteFramebuffers(1, &g_game_post_fbo);
        if (g_game_post_tex) gl->DeleteTextures(1, &g_game_post_tex);
        g_game_post_fbo = 0;
        g_game_post_tex = 0;
        g_game_post_w = 0;
        g_game_post_h = 0;
    }
    if (!g_game_post_fbo) {
        gl->GenTextures(1, &g_game_post_tex);
        if (!g_game_post_tex) return 0;
        gl->BindTexture(ZGL_TEXTURE_2D, g_game_post_tex);
        gl->TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA8, frame->width, frame->height,
            0, ZGL_RGBA, ZGL_UNSIGNED_BYTE, NULL);
        gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_LINEAR);
        gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_LINEAR);
        gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
        gl->TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
        gl->GenFramebuffers(1, &g_game_post_fbo);
        if (!g_game_post_fbo) {
            gl->DeleteTextures(1, &g_game_post_tex);
            g_game_post_tex = 0;
            return 0;
        }
        gl->BindFramebuffer(ZGL_FRAMEBUFFER, g_game_post_fbo);
        gl->FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
            ZGL_TEXTURE_2D, g_game_post_tex, 0);
        if (gl->CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE ||
            gl->GetError() != ZGL_NO_ERROR) {
            gl->DeleteFramebuffers(1, &g_game_post_fbo);
            gl->DeleteTextures(1, &g_game_post_tex);
            g_game_post_fbo = 0;
            g_game_post_tex = 0;
            return 0;
        }
        g_game_post_w = frame->width;
        g_game_post_h = frame->height;
    }
    if (!g_game_post_vao) {
        gl->GenVertexArrays(1, &g_game_post_vao);
        if (!g_game_post_vao) return 0;
    }
    return 1;
}

ZAN_GAME_API int32_t zan_game_postprocess3d(int32_t surface_id, float exposure,
    int32_t tonemap_mode, float vignette, float bloom_threshold,
    float bloom_intensity) {
    if (surface_id < 0 || surface_id >= ZAN_GAME_SURFACE_CAP) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_post_program *prog = zan_game_post_program_get();
    if (!prog) goto done;
    if (!zan_game_post_target_ready(&frame)) goto done;

    int clip_x, clip_y, clip_w, clip_h;
    zan_game_clip(&frame, &clip_x, &clip_y, &clip_w, &clip_h);
    if (!clip_w || !clip_h) { result = 1; goto done; }

    const zan_gl_api *gl = frame.api;
    gl->Disable(ZGL_SCISSOR_TEST);
    gl->Disable(ZGL_DEPTH_TEST);
    gl->Disable(ZGL_BLEND);

    /* 纯 GPU VRAM 拷贝原帧颜色到离屏纹理，杜绝同纹理读写反馈循环。 */
    gl->BindFramebuffer(ZGL_READ_FRAMEBUFFER, frame.framebuffer);
    gl->BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, g_game_post_fbo);
    gl->BlitFramebuffer(0, 0, frame.width, frame.height,
        0, 0, frame.width, frame.height, ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);

    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    gl->Viewport(0, 0, frame.width, frame.height);
    gl->Enable(ZGL_SCISSOR_TEST);
    gl->Scissor(clip_x, clip_y, clip_w, clip_h);
    gl->UseProgram(prog->program);
    gl->ActiveTexture(ZGL_TEXTURE0);
    gl->BindTexture(ZGL_TEXTURE_2D, g_game_post_tex);
    if (prog->u_scene >= 0) gl->Uniform1i(prog->u_scene, 0);
    if (prog->u_texel_size >= 0) {
        gl->Uniform2f(prog->u_texel_size,
            1.0f / (float)frame.width, 1.0f / (float)frame.height);
    }
    if (prog->u_exposure >= 0 && gl->Uniform1f) gl->Uniform1f(prog->u_exposure, exposure);
    if (prog->u_tonemap >= 0) gl->Uniform1i(prog->u_tonemap, tonemap_mode);
    if (prog->u_vignette >= 0 && gl->Uniform1f) gl->Uniform1f(prog->u_vignette, vignette);
    if (prog->u_bloom >= 0) gl->Uniform2f(prog->u_bloom, bloom_threshold, bloom_intensity);
    if (prog->u_color_grade >= 0) {
        gl->Uniform2f(prog->u_color_grade, g_game_color_grade[0], g_game_color_grade[1]);
    }
    if (prog->u_color_tint >= 0) {
        gl->Uniform3fv(prog->u_color_tint, 1, g_game_color_tint);
    }
    if (prog->u_post_lens >= 0) {
        gl->Uniform2f(prog->u_post_lens, g_game_post_lens[0], g_game_post_lens[1]);
    }
    if (prog->u_post_film >= 0) {
        gl->Uniform3fv(prog->u_post_film, 1, g_game_post_film);
    }
    if (prog->u_post_cinema >= 0) {
        gl->Uniform4f(prog->u_post_cinema,
            g_game_post_cinema[0], g_game_post_cinema[1],
            g_game_post_cinema[2], g_game_post_cinema[3]);
    }
    if (prog->u_post_radial_blur >= 0) {
        gl->Uniform3fv(prog->u_post_radial_blur, 1, g_game_post_radial_blur);
    }
    if (prog->u_post_pixelate >= 0) {
        gl->Uniform2f(prog->u_post_pixelate, g_game_post_pixelate[0], g_game_post_pixelate[1]);
    }
    if (prog->u_post_duotone_dark >= 0) {
        gl->Uniform4f(prog->u_post_duotone_dark,
            g_game_post_duotone_dark[0], g_game_post_duotone_dark[1],
            g_game_post_duotone_dark[2], g_game_post_duotone_dark[3]);
    }
    if (prog->u_post_duotone_light >= 0) {
        gl->Uniform3fv(prog->u_post_duotone_light, 1, g_game_post_duotone_light);
    }
    if (prog->u_post_crt >= 0) {
        gl->Uniform4f(prog->u_post_crt,
            g_game_post_crt[0], g_game_post_crt[1],
            g_game_post_crt[2], g_game_post_crt[3]);
    }
    if (prog->u_post_outline_params >= 0) {
        gl->Uniform2f(prog->u_post_outline_params,
            g_game_post_outline_params[0], g_game_post_outline_params[1]);
    }
    if (prog->u_post_outline_color >= 0) {
        gl->Uniform3fv(prog->u_post_outline_color, 1, g_game_post_outline_color);
    }
    if (prog->u_post_thermal >= 0) {
        gl->Uniform3fv(prog->u_post_thermal, 1, g_game_post_thermal);
    }
    if (prog->u_post_flash_params >= 0) {
        gl->Uniform2f(prog->u_post_flash_params,
            g_game_post_flash_params[0], g_game_post_flash_params[1]);
    }
    if (prog->u_post_flash_color >= 0) {
        gl->Uniform3fv(prog->u_post_flash_color, 1, g_game_post_flash_color);
    }
    if (prog->u_post_night_vision >= 0) {
        gl->Uniform4f(prog->u_post_night_vision,
            g_game_post_night_vision[0], g_game_post_night_vision[1],
            g_game_post_night_vision[2], g_game_post_night_vision[3]);
    }
    if (prog->u_post_halftone >= 0) {
        gl->Uniform3fv(prog->u_post_halftone, 1, g_game_post_halftone);
    }
    if (prog->u_post_glitch >= 0) {
        gl->Uniform3fv(prog->u_post_glitch, 1, g_game_post_glitch);
    }
    if (prog->u_post_tilt_shift >= 0) {
        gl->Uniform3fv(prog->u_post_tilt_shift, 1, g_game_post_tilt_shift);
    }
    if (prog->u_post_frost_params >= 0) {
        gl->Uniform2f(prog->u_post_frost_params,
            g_game_post_frost_params[0], g_game_post_frost_params[1]);
    }
    if (prog->u_post_frost_color >= 0) {
        gl->Uniform3fv(prog->u_post_frost_color, 1, g_game_post_frost_color);
    }
    if (prog->u_post_sepia >= 0) {
        gl->Uniform3fv(prog->u_post_sepia, 1, g_game_post_sepia);
    }
    if (prog->u_post_streak_params >= 0) {
        gl->Uniform2f(prog->u_post_streak_params,
            g_game_post_streak_params[0], g_game_post_streak_params[1]);
    }
    if (prog->u_post_streak_color >= 0) {
        gl->Uniform3fv(prog->u_post_streak_color, 1, g_game_post_streak_color);
    }
    if (prog->u_post_split_params >= 0) {
        gl->Uniform2f(prog->u_post_split_params,
            g_game_post_split_params[0], g_game_post_split_params[1]);
    }
    if (prog->u_post_split_shadow >= 0) {
        gl->Uniform3fv(prog->u_post_split_shadow, 1, g_game_post_split_shadow);
    }
    if (prog->u_post_split_high >= 0) {
        gl->Uniform3fv(prog->u_post_split_high, 1, g_game_post_split_high);
    }
    if (prog->u_post_oil_paint >= 0) {
        gl->Uniform2f(prog->u_post_oil_paint,
            g_game_post_oil_paint[0], g_game_post_oil_paint[1]);
    }
    if (prog->u_post_scope_blur >= 0) {
        gl->Uniform2f(prog->u_post_scope_blur,
            g_game_post_scope_blur[0], g_game_post_scope_blur[1]);
    }
    if (prog->u_post_daltonize >= 0) {
        gl->Uniform2f(prog->u_post_daltonize,
            g_game_post_daltonize[0], g_game_post_daltonize[1]);
    }
    gl->BindVertexArray(g_game_post_vao);
    gl->DrawArrays(ZGL_TRIANGLES, 0, 3);
    gl->BindVertexArray(0);
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}

ZAN_GAME_API void zan_game_set_normal_map3d(const char *texture, float scale) {
    if (!texture || !texture[0] || scale <= 0.0f) {
        g_game_normal_key[0] = '\0';
        g_game_normal_scale = 0.0f;
        return;
    }
    snprintf(g_game_normal_key, sizeof(g_game_normal_key), "%s", texture);
    g_game_normal_scale = scale;
}

static zan_game_skybox_program *zan_game_skybox_program_get(void) {
    if (g_game_skybox_prog.program) return &g_game_skybox_prog;
    char vs_src[1024];
    char fs_src[2048];
    const char *head = zan_game_glsl_head();
    snprintf(vs_src, sizeof(vs_src), "%s%s", head, ZAN_GAME_POST_VS);
    snprintf(fs_src, sizeof(fs_src), "%s%s", head, ZAN_GAME_SKYBOX_FS);
    zgl_uint prog = zan_gui_gpu_program(vs_src, fs_src);
    if (!prog) return NULL;
    g_game_skybox_prog.program = prog;
    g_game_skybox_prog.u_cam_right = g_game_gl->GetUniformLocation(prog, "uCamRight");
    g_game_skybox_prog.u_cam_up = g_game_gl->GetUniformLocation(prog, "uCamUp");
    g_game_skybox_prog.u_cam_forward = g_game_gl->GetUniformLocation(prog, "uCamForward");
    g_game_skybox_prog.u_proj = g_game_gl->GetUniformLocation(prog, "uProj");
    g_game_skybox_prog.u_sun_dir = g_game_gl->GetUniformLocation(prog, "uSunDir");
    g_game_skybox_prog.u_sky_top = g_game_gl->GetUniformLocation(prog, "uSkyTop");
    g_game_skybox_prog.u_sky_horizon = g_game_gl->GetUniformLocation(prog, "uSkyHorizon");
    g_game_skybox_prog.u_ground = g_game_gl->GetUniformLocation(prog, "uGround");
    return &g_game_skybox_prog;
}

ZAN_GAME_API int32_t zan_game_skybox3d(int32_t surface_id,
    const float *cam_basis9, float tan_half_fov, float aspect,
    const float *sun_dir3, const float *top_rgb3, const float *horizon_rgb3,
    const float *ground_rgb3) {
    if (surface_id < 0 || surface_id >= ZAN_GAME_SURFACE_CAP || !cam_basis9) return 0;
    zan_gpu_frame frame;
    if (!zan_gui_gpu_begin(surface_id, &frame)) return 0;
    int32_t result = 0;
    if (!zan_game_gpu_prepare(&frame)) goto done;
    zan_game_skybox_program *prog = zan_game_skybox_program_get();
    if (!prog) goto done;
    const zan_gl_api *gl = frame.api;
    if (!g_game_post_vao) {
        gl->GenVertexArrays(1, &g_game_post_vao);
        if (!g_game_post_vao) goto done;
    }

    int clip_x, clip_y, clip_w, clip_h;
    zan_game_clip(&frame, &clip_x, &clip_y, &clip_w, &clip_h);
    if (!clip_w || !clip_h) { result = 1; goto done; }

    float sun_d[3] = { 0.4f, 0.7f, 0.5f };
    float top_c[3] = { 0.14f, 0.36f, 0.76f };
    float hor_c[3] = { 0.68f, 0.80f, 0.94f };
    float gnd_c[3] = { 0.24f, 0.22f, 0.20f };
    if (sun_dir3) { sun_d[0] = sun_dir3[0]; sun_d[1] = sun_dir3[1]; sun_d[2] = sun_dir3[2]; }
    if (top_rgb3) { top_c[0] = top_rgb3[0]; top_c[1] = top_rgb3[1]; top_c[2] = top_rgb3[2]; }
    if (horizon_rgb3) { hor_c[0] = horizon_rgb3[0]; hor_c[1] = horizon_rgb3[1]; hor_c[2] = horizon_rgb3[2]; }
    if (ground_rgb3) { gnd_c[0] = ground_rgb3[0]; gnd_c[1] = ground_rgb3[1]; gnd_c[2] = ground_rgb3[2]; }
    if (tan_half_fov <= 0.001f) tan_half_fov = 0.57735f;
    if (aspect <= 0.001f) aspect = (float)frame.width / (float)(frame.height > 0 ? frame.height : 1);

    gl->BindFramebuffer(ZGL_FRAMEBUFFER, frame.framebuffer);
    gl->Viewport(0, 0, frame.width, frame.height);
    gl->Enable(ZGL_SCISSOR_TEST);
    gl->Scissor(clip_x, clip_y, clip_w, clip_h);
    gl->Disable(ZGL_DEPTH_TEST);
    if (gl->DepthMask) gl->DepthMask(ZGL_FALSE);
    gl->Disable(ZGL_BLEND);

    gl->UseProgram(prog->program);
    if (prog->u_cam_right >= 0) gl->Uniform3fv(prog->u_cam_right, 1, &cam_basis9[0]);
    if (prog->u_cam_up >= 0) gl->Uniform3fv(prog->u_cam_up, 1, &cam_basis9[3]);
    if (prog->u_cam_forward >= 0) gl->Uniform3fv(prog->u_cam_forward, 1, &cam_basis9[6]);
    if (prog->u_proj >= 0) gl->Uniform2f(prog->u_proj, tan_half_fov * aspect, tan_half_fov);
    if (prog->u_sun_dir >= 0) gl->Uniform3fv(prog->u_sun_dir, 1, sun_d);
    if (prog->u_sky_top >= 0) gl->Uniform3fv(prog->u_sky_top, 1, top_c);
    if (prog->u_sky_horizon >= 0) gl->Uniform3fv(prog->u_sky_horizon, 1, hor_c);
    if (prog->u_ground >= 0) gl->Uniform3fv(prog->u_ground, 1, gnd_c);

    gl->BindVertexArray(g_game_post_vao);
    gl->DrawArrays(ZGL_TRIANGLES, 0, 3);
    gl->BindVertexArray(0);
    if (gl->DepthMask) gl->DepthMask(ZGL_TRUE);
    gl->Finish();
    result = gl->GetError() == ZGL_NO_ERROR;

done:
    zan_gui_gpu_end(surface_id);
    return result;
}

ZAN_GAME_API int32_t zan_game_set_spot_lights3d(int32_t count,
    const float *pos_range4, const float *dir_inner4, const float *color_outer4) {
    if (count < 0 || count > ZAN_GAME_MAX_LIGHTS) return 0;
    if (count > 0 && (!pos_range4 || !dir_inner4 || !color_outer4)) return 0;
    g_game_spot_count = count;
    for (int i = 0; i < count; ++i) {
        g_game_spot_pos[i][0] = pos_range4[i * 4 + 0];
        g_game_spot_pos[i][1] = pos_range4[i * 4 + 1];
        g_game_spot_pos[i][2] = pos_range4[i * 4 + 2];
        g_game_spot_pos[i][3] = pos_range4[i * 4 + 3];
        g_game_spot_dir[i][0] = dir_inner4[i * 4 + 0];
        g_game_spot_dir[i][1] = dir_inner4[i * 4 + 1];
        g_game_spot_dir[i][2] = dir_inner4[i * 4 + 2];
        g_game_spot_dir[i][3] = dir_inner4[i * 4 + 3];
        g_game_spot_color[i][0] = color_outer4[i * 4 + 0];
        g_game_spot_color[i][1] = color_outer4[i * 4 + 1];
        g_game_spot_color[i][2] = color_outer4[i * 4 + 2];
        g_game_spot_color[i][3] = color_outer4[i * 4 + 3];
    }
    ++g_game_light_version;
    return 1;
}

ZAN_GAME_API void zan_game_set_hemi_ambient3d(int32_t enabled,
    float sky_r, float sky_g, float sky_b,
    float ground_r, float ground_g, float ground_b) {
    g_game_hemi_on = enabled ? 1 : 0;
    g_game_hemi_sky[0] = sky_r;
    g_game_hemi_sky[1] = sky_g;
    g_game_hemi_sky[2] = sky_b;
    g_game_hemi_ground[0] = ground_r;
    g_game_hemi_ground[1] = ground_g;
    g_game_hemi_ground[2] = ground_b;
    ++g_game_light_version;
}

ZAN_GAME_API void zan_game_set_pbr_maps3d(const char *orm_texture,
    float ao_strength) {
    if (!orm_texture || !orm_texture[0]) {
        g_game_orm_key[0] = '\0';
        g_game_ao_strength = 1.0f;
        return;
    }
    snprintf(g_game_orm_key, sizeof(g_game_orm_key), "%s", orm_texture);
    if (ao_strength < 0.0f) ao_strength = 0.0f;
    if (ao_strength > 1.0f) ao_strength = 1.0f;
    g_game_ao_strength = ao_strength;
}

ZAN_GAME_API void zan_game_set_alpha_cutoff3d(float cutoff) {
    if (cutoff <= 0.0f) {
        g_game_alpha_cutoff = 0.0f;
        return;
    }
    if (cutoff > 1.0f) cutoff = 1.0f;
    g_game_alpha_cutoff = cutoff;
}

ZAN_GAME_API void zan_game_set_emissive_map3d(const char *texture) {
    if (!texture || !texture[0]) {
        g_game_emissive_key[0] = '\0';
        return;
    }
    snprintf(g_game_emissive_key, sizeof(g_game_emissive_key), "%s", texture);
}

ZAN_GAME_API void zan_game_set_uv_transform3d(float scale_u, float scale_v,
    float offset_u, float offset_v) {
    g_game_uv_transform[0] = scale_u;
    g_game_uv_transform[1] = scale_v;
    g_game_uv_transform[2] = offset_u;
    g_game_uv_transform[3] = offset_v;
}

ZAN_GAME_API void zan_game_set_rim_light3d(float r, float g, float b, float power) {
    g_game_rim_light[0] = r > 0.0f ? r : 0.0f;
    g_game_rim_light[1] = g > 0.0f ? g : 0.0f;
    g_game_rim_light[2] = b > 0.0f ? b : 0.0f;
    g_game_rim_light[3] = power >= 0.25f ? power : 0.25f;
}

ZAN_GAME_API void zan_game_set_color_grade3d(float saturation, float contrast,
    float tint_r, float tint_g, float tint_b) {
    g_game_color_grade[0] = saturation >= 0.0f ? saturation : 0.0f;
    g_game_color_grade[1] = contrast >= 0.0f ? contrast : 0.0f;
    g_game_color_tint[0] = tint_r >= 0.0f ? tint_r : 0.0f;
    g_game_color_tint[1] = tint_g >= 0.0f ? tint_g : 0.0f;
    g_game_color_tint[2] = tint_b >= 0.0f ? tint_b : 0.0f;
}

ZAN_GAME_API void zan_game_set_toon_shading3d(float bands, float specular_step) {
    g_game_toon[0] = bands >= 2.0f ? bands : 0.0f;
    if (specular_step < 0.05f) specular_step = 0.05f;
    if (specular_step > 0.98f) specular_step = 0.98f;
    g_game_toon[1] = specular_step;
}

ZAN_GAME_API void zan_game_set_post_lens3d(float fxaa_strength, float chromatic_strength) {
    if (fxaa_strength < 0.0f) fxaa_strength = 0.0f;
    if (fxaa_strength > 1.0f) fxaa_strength = 1.0f;
    if (chromatic_strength < 0.0f) chromatic_strength = 0.0f;
    if (chromatic_strength > 0.1f) chromatic_strength = 0.1f;
    g_game_post_lens[0] = fxaa_strength;
    g_game_post_lens[1] = chromatic_strength;
}

ZAN_GAME_API void zan_game_set_dissolve3d(float threshold, float edge_width,
    float edge_r, float edge_g, float edge_b) {
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.2f) threshold = 1.2f;
    if (edge_width < 0.0f) edge_width = 0.0f;
    if (edge_width > 0.5f) edge_width = 0.5f;
    g_game_dissolve[0] = threshold;
    g_game_dissolve[1] = edge_width;
    g_game_dissolve_edge[0] = edge_r > 0.0f ? edge_r : 0.0f;
    g_game_dissolve_edge[1] = edge_g > 0.0f ? edge_g : 0.0f;
    g_game_dissolve_edge[2] = edge_b > 0.0f ? edge_b : 0.0f;
}

ZAN_GAME_API void zan_game_set_clip_plane3d(int32_t enabled,
    float nx, float ny, float nz, float d) {
    g_game_clip_on = enabled ? 1 : 0;
    g_game_clip_plane[0] = nx;
    g_game_clip_plane[1] = ny;
    g_game_clip_plane[2] = nz;
    g_game_clip_plane[3] = d;
}

ZAN_GAME_API void zan_game_set_triplanar3d(int32_t enabled, float scale,
    float sharpness) {
    g_game_triplanar[0] = enabled ? 1.0f : 0.0f;
    g_game_triplanar[1] = scale > 0.0001f ? scale : 1.0f;
    g_game_triplanar[2] = sharpness >= 1.0f ? sharpness : 4.0f;
}

ZAN_GAME_API void zan_game_set_height_fog3d(int32_t enabled, float base_y,
    float falloff, float max_density, float r, float g, float b) {
    g_game_height_fog_params[0] = base_y;
    g_game_height_fog_params[1] = falloff > 0.001f ? falloff : 0.001f;
    if (max_density < 0.0f) max_density = 0.0f;
    if (max_density > 1.0f) max_density = 1.0f;
    g_game_height_fog_params[2] = max_density;
    g_game_height_fog_params[3] = enabled ? 1.0f : 0.0f;
    g_game_height_fog_color[0] = r > 0.0f ? r : 0.0f;
    g_game_height_fog_color[1] = g > 0.0f ? g : 0.0f;
    g_game_height_fog_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_hit_flash3d(float r, float g, float b, float amount) {
    g_game_hit_flash[0] = r > 0.0f ? r : 0.0f;
    g_game_hit_flash[1] = g > 0.0f ? g : 0.0f;
    g_game_hit_flash[2] = b > 0.0f ? b : 0.0f;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_game_hit_flash[3] = amount;
}

ZAN_GAME_API void zan_game_set_post_film3d(float sharpen_strength,
    float grain_strength, float grain_seed) {
    if (sharpen_strength < 0.0f) sharpen_strength = 0.0f;
    if (sharpen_strength > 2.0f) sharpen_strength = 2.0f;
    if (grain_strength < 0.0f) grain_strength = 0.0f;
    if (grain_strength > 0.5f) grain_strength = 0.5f;
    g_game_post_film[0] = sharpen_strength;
    g_game_post_film[1] = grain_strength;
    g_game_post_film[2] = grain_seed;
}

ZAN_GAME_API void zan_game_set_holo_scan3d(int32_t enabled, float freq,
    float phase, float strength, float r, float g, float b) {
    g_game_holo_scan_params[0] = enabled ? 1.0f : 0.0f;
    g_game_holo_scan_params[1] = freq > 0.001f ? freq : 12.0f;
    g_game_holo_scan_params[2] = phase;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 2.0f) strength = 2.0f;
    g_game_holo_scan_params[3] = strength;
    g_game_holo_scan_color[0] = r > 0.0f ? r : 0.0f;
    g_game_holo_scan_color[1] = g > 0.0f ? g : 0.0f;
    g_game_holo_scan_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_cinema3d(float letterbox_ratio,
    float distort_amp, float distort_freq, float distort_phase) {
    if (letterbox_ratio < 0.0f) letterbox_ratio = 0.0f;
    if (letterbox_ratio > 0.45f) letterbox_ratio = 0.45f;
    if (distort_amp < 0.0f) distort_amp = 0.0f;
    if (distort_amp > 0.08f) distort_amp = 0.08f;
    g_game_post_cinema[0] = letterbox_ratio;
    g_game_post_cinema[1] = distort_amp;
    g_game_post_cinema[2] = distort_freq > 0.001f ? distort_freq : 16.0f;
    g_game_post_cinema[3] = distort_phase;
}

ZAN_GAME_API void zan_game_set_top_cover3d(int32_t enabled, float threshold,
    float sharpness, float amount, float r, float g, float b) {
    g_game_top_cover_params[0] = enabled ? 1.0f : 0.0f;
    if (threshold < -1.0f) threshold = -1.0f;
    if (threshold > 1.0f) threshold = 1.0f;
    g_game_top_cover_params[1] = threshold;
    if (sharpness < 0.01f) sharpness = 0.01f;
    if (sharpness > 1.0f) sharpness = 1.0f;
    g_game_top_cover_params[2] = sharpness;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_game_top_cover_params[3] = amount;
    g_game_top_cover_color[0] = r > 0.0f ? r : 0.0f;
    g_game_top_cover_color[1] = g > 0.0f ? g : 0.0f;
    g_game_top_cover_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_radial_blur3d(float strength,
    float center_x, float center_y) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 0.15f) strength = 0.15f;
    if (center_x < 0.0f) center_x = 0.0f;
    if (center_x > 1.0f) center_x = 1.0f;
    if (center_y < 0.0f) center_y = 0.0f;
    if (center_y > 1.0f) center_y = 1.0f;
    g_game_post_radial_blur[0] = strength;
    g_game_post_radial_blur[1] = center_x;
    g_game_post_radial_blur[2] = center_y;
}

ZAN_GAME_API void zan_game_set_wind_sway3d(int32_t enabled, float amplitude,
    float freq, float phase, float height_scale) {
    if (!enabled || amplitude <= 0.0f) {
        g_game_wind_params[0] = 0.0f;
        return;
    }
    if (amplitude > 2.0f) amplitude = 2.0f;
    g_game_wind_params[0] = amplitude;
    g_game_wind_params[1] = freq > 0.0f ? freq : 1.5f;
    g_game_wind_params[2] = phase;
    g_game_wind_params[3] = height_scale > 0.0f ? height_scale : 1.0f;
}

ZAN_GAME_API void zan_game_set_post_pixelate3d(float pixel_size,
    float posterize_levels) {
    if (pixel_size < 0.0f) pixel_size = 0.0f;
    if (pixel_size > 64.0f) pixel_size = 64.0f;
    if (posterize_levels < 0.0f) posterize_levels = 0.0f;
    if (posterize_levels > 64.0f) posterize_levels = 64.0f;
    g_game_post_pixelate[0] = pixel_size;
    g_game_post_pixelate[1] = posterize_levels;
}

ZAN_GAME_API void zan_game_set_distance_fade3d(int32_t enabled,
    float start_dist, float end_dist) {
    g_game_dist_fade[0] = enabled ? 1.0f : 0.0f;
    g_game_dist_fade[1] = start_dist;
    g_game_dist_fade[2] = end_dist;
}

ZAN_GAME_API void zan_game_set_post_duotone3d(float strength,
    float dark_r, float dark_g, float dark_b,
    float light_r, float light_g, float light_b) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    g_game_post_duotone_dark[0] = dark_r > 0.0f ? dark_r : 0.0f;
    g_game_post_duotone_dark[1] = dark_g > 0.0f ? dark_g : 0.0f;
    g_game_post_duotone_dark[2] = dark_b > 0.0f ? dark_b : 0.0f;
    g_game_post_duotone_dark[3] = strength;
    g_game_post_duotone_light[0] = light_r > 0.0f ? light_r : 0.0f;
    g_game_post_duotone_light[1] = light_g > 0.0f ? light_g : 0.0f;
    g_game_post_duotone_light[2] = light_b > 0.0f ? light_b : 0.0f;
}

ZAN_GAME_API void zan_game_set_world_grid3d(int32_t enabled, float cell_size,
    float line_width, float strength, float r, float g, float b) {
    g_game_world_grid_params[0] = enabled ? 1.0f : 0.0f;
    g_game_world_grid_params[1] = cell_size > 0.05f ? cell_size : 1.0f;
    if (line_width < 0.005f) line_width = 0.005f;
    if (line_width > 0.45f) line_width = 0.45f;
    g_game_world_grid_params[2] = line_width;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    g_game_world_grid_params[3] = strength;
    g_game_world_grid_color[0] = r > 0.0f ? r : 0.0f;
    g_game_world_grid_color[1] = g > 0.0f ? g : 0.0f;
    g_game_world_grid_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_crt_scanline3d(float scanline_intensity,
    float scanline_count, float curvature, float glow_boost) {
    if (scanline_intensity < 0.0f) scanline_intensity = 0.0f;
    if (scanline_intensity > 0.95f) scanline_intensity = 0.95f;
    if (scanline_count < 2.0f) scanline_count = 64.0f;
    if (curvature < 0.0f) curvature = 0.0f;
    if (curvature > 0.5f) curvature = 0.5f;
    if (glow_boost < 0.1f) glow_boost = 1.0f;
    g_game_post_crt[0] = scanline_intensity;
    g_game_post_crt[1] = scanline_count;
    g_game_post_crt[2] = curvature;
    g_game_post_crt[3] = glow_boost;
}

ZAN_GAME_API void zan_game_set_water_caustics3d(int32_t enabled, float scale,
    float phase, float strength, float r, float g, float b) {
    g_game_caustics_params[0] = enabled ? 1.0f : 0.0f;
    g_game_caustics_params[1] = scale > 0.1f ? scale : 1.0f;
    g_game_caustics_params[2] = phase;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 2.0f) strength = 2.0f;
    g_game_caustics_params[3] = strength;
    g_game_caustics_color[0] = r > 0.0f ? r : 0.0f;
    g_game_caustics_color[1] = g > 0.0f ? g : 0.0f;
    g_game_caustics_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_edge_outline3d(float strength,
    float threshold, float r, float g, float b) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (threshold < 0.02f) threshold = 0.02f;
    if (threshold > 1.0f) threshold = 1.0f;
    g_game_post_outline_params[0] = strength;
    g_game_post_outline_params[1] = threshold;
    g_game_post_outline_color[0] = r > 0.0f ? r : 0.0f;
    g_game_post_outline_color[1] = g > 0.0f ? g : 0.0f;
    g_game_post_outline_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_ice_frost3d(int32_t enabled, float scale,
    float glint_strength, float fresnel_power, float r, float g, float b) {
    g_game_ice_frost_params[0] = enabled ? 1.0f : 0.0f;
    g_game_ice_frost_params[1] = scale > 0.2f ? scale : 2.0f;
    if (glint_strength < 0.0f) glint_strength = 0.0f;
    if (glint_strength > 2.0f) glint_strength = 2.0f;
    g_game_ice_frost_params[2] = glint_strength;
    if (fresnel_power < 0.5f) fresnel_power = 0.5f;
    if (fresnel_power > 8.0f) fresnel_power = 8.0f;
    g_game_ice_frost_params[3] = fresnel_power;
    g_game_ice_frost_color[0] = r > 0.0f ? r : 0.0f;
    g_game_ice_frost_color[1] = g > 0.0f ? g : 0.0f;
    g_game_ice_frost_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_thermal_heat3d(float strength,
    float sensitivity, float cold_shift) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (sensitivity < 0.1f) sensitivity = 0.1f;
    if (sensitivity > 5.0f) sensitivity = 5.0f;
    if (cold_shift < -0.5f) cold_shift = -0.5f;
    if (cold_shift > 0.5f) cold_shift = 0.5f;
    g_game_post_thermal[0] = strength;
    g_game_post_thermal[1] = sensitivity;
    g_game_post_thermal[2] = cold_shift;
}

ZAN_GAME_API void zan_game_set_lava_crust3d(int32_t enabled, float scale,
    float phase, float crack_width, float strength, float r, float g, float b) {
    g_game_lava_crust_params[0] = enabled ? 1.0f : 0.0f;
    g_game_lava_crust_params[1] = scale > 0.2f ? scale : 1.5f;
    g_game_lava_crust_params[2] = phase;
    if (crack_width < 0.02f) crack_width = 0.02f;
    if (crack_width > 0.6f) crack_width = 0.6f;
    g_game_lava_crust_params[3] = crack_width;
    g_game_lava_crust_color[0] = r > 0.0f ? r : 0.0f;
    g_game_lava_crust_color[1] = g > 0.0f ? g : 0.0f;
    g_game_lava_crust_color[2] = b > 0.0f ? b : 0.0f;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_lava_crust_color[3] = strength;
}

ZAN_GAME_API void zan_game_set_post_screen_tint_flash3d(float edge_strength,
    float flash_strength, float r, float g, float b) {
    if (edge_strength < 0.0f) edge_strength = 0.0f;
    if (edge_strength > 1.0f) edge_strength = 1.0f;
    if (flash_strength < 0.0f) flash_strength = 0.0f;
    if (flash_strength > 1.0f) flash_strength = 1.0f;
    g_game_post_flash_params[0] = edge_strength;
    g_game_post_flash_params[1] = flash_strength;
    g_game_post_flash_color[0] = r > 0.0f ? r : 0.0f;
    g_game_post_flash_color[1] = g > 0.0f ? g : 0.0f;
    g_game_post_flash_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_iridescence3d(int32_t enabled,
    float film_thickness, float phase_shift, float strength) {
    g_game_iridescence_params[0] = enabled ? 1.0f : 0.0f;
    if (film_thickness < 0.5f) film_thickness = 0.5f;
    if (film_thickness > 6.0f) film_thickness = 6.0f;
    g_game_iridescence_params[1] = film_thickness;
    g_game_iridescence_params[2] = phase_shift;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 2.0f) strength = 2.0f;
    g_game_iridescence_params[3] = strength;
}

ZAN_GAME_API void zan_game_set_post_night_vision3d(float strength, float gain,
    float noise_amount, float seed) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (gain < 0.25f) gain = 0.25f;
    if (gain > 8.0f) gain = 8.0f;
    if (noise_amount < 0.0f) noise_amount = 0.0f;
    if (noise_amount > 0.5f) noise_amount = 0.5f;
    g_game_post_night_vision[0] = strength;
    g_game_post_night_vision[1] = gain;
    g_game_post_night_vision[2] = noise_amount;
    g_game_post_night_vision[3] = seed;
}

ZAN_GAME_API void zan_game_set_sonar_pulse3d(int32_t enabled,
    float origin_x, float origin_y, float origin_z,
    float radius, float ring_width, float strength, float r, float g, float b) {
    g_game_sonar_origin_radius[0] = origin_x;
    g_game_sonar_origin_radius[1] = origin_y;
    g_game_sonar_origin_radius[2] = origin_z;
    g_game_sonar_origin_radius[3] = radius > 0.05f ? radius : 0.05f;
    g_game_sonar_style[0] = enabled ? 1.0f : 0.0f;
    g_game_sonar_style[1] = ring_width > 0.05f ? ring_width : 0.05f;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_sonar_style[2] = strength;
    g_game_sonar_color[0] = r > 0.0f ? r : 0.0f;
    g_game_sonar_color[1] = g > 0.0f ? g : 0.0f;
    g_game_sonar_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_halftone_comic3d(float strength,
    float dot_scale, float contrast) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (dot_scale < 2.0f) dot_scale = 2.0f;
    if (dot_scale > 32.0f) dot_scale = 32.0f;
    if (contrast < 0.25f) contrast = 0.25f;
    if (contrast > 3.0f) contrast = 3.0f;
    g_game_post_halftone[0] = strength;
    g_game_post_halftone[1] = dot_scale;
    g_game_post_halftone[2] = contrast;
}

ZAN_GAME_API void zan_game_set_force_field_hex3d(int32_t enabled,
    float scale, float phase, float edge_width, float strength,
    float r, float g, float b) {
    g_game_force_field_params[0] = enabled ? 1.0f : 0.0f;
    g_game_force_field_params[1] = scale > 0.25f ? scale : 2.0f;
    g_game_force_field_params[2] = phase;
    if (edge_width < 0.02f) edge_width = 0.02f;
    if (edge_width > 0.35f) edge_width = 0.35f;
    g_game_force_field_params[3] = edge_width;
    g_game_force_field_color[0] = r > 0.0f ? r : 0.0f;
    g_game_force_field_color[1] = g > 0.0f ? g : 0.0f;
    g_game_force_field_color[2] = b > 0.0f ? b : 0.0f;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_force_field_color[3] = strength;
}

ZAN_GAME_API void zan_game_set_post_glitch_rgb3d(float intensity,
    float slice_count, float seed) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    if (slice_count < 4.0f) slice_count = 4.0f;
    if (slice_count > 128.0f) slice_count = 128.0f;
    g_game_post_glitch[0] = intensity;
    g_game_post_glitch[1] = slice_count;
    g_game_post_glitch[2] = seed;
}

ZAN_GAME_API void zan_game_set_rain_ripples3d(int32_t enabled, float scale,
    float phase, float strength, float r, float g, float b) {
    g_game_rain_ripples_params[0] = enabled ? 1.0f : 0.0f;
    g_game_rain_ripples_params[1] = scale > 0.25f ? scale : 2.0f;
    g_game_rain_ripples_params[2] = phase;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_rain_ripples_params[3] = strength;
    g_game_rain_ripples_color[0] = r > 0.0f ? r : 0.0f;
    g_game_rain_ripples_color[1] = g > 0.0f ? g : 0.0f;
    g_game_rain_ripples_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_tilt_shift3d(float strength,
    float focus_y, float focus_band) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (focus_y < 0.0f) focus_y = 0.0f;
    if (focus_y > 1.0f) focus_y = 1.0f;
    if (focus_band < 0.02f) focus_band = 0.02f;
    if (focus_band > 0.45f) focus_band = 0.45f;
    g_game_post_tilt_shift[0] = strength;
    g_game_post_tilt_shift[1] = focus_y;
    g_game_post_tilt_shift[2] = focus_band;
}

ZAN_GAME_API void zan_game_set_xray_silhouette3d(int32_t enabled, float power,
    float strength, float r, float g, float b) {
    g_game_xray_params[0] = enabled ? 1.0f : 0.0f;
    if (power < 0.5f) power = 0.5f;
    if (power > 8.0f) power = 8.0f;
    g_game_xray_params[1] = power;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_xray_params[2] = strength;
    g_game_xray_color[0] = r > 0.0f ? r : 0.0f;
    g_game_xray_color[1] = g > 0.0f ? g : 0.0f;
    g_game_xray_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_vignette_frost3d(float intensity,
    float radius, float r, float g, float b) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    if (radius < 0.15f) radius = 0.15f;
    if (radius > 0.95f) radius = 0.95f;
    g_game_post_frost_params[0] = intensity;
    g_game_post_frost_params[1] = radius;
    g_game_post_frost_color[0] = r > 0.0f ? r : 0.0f;
    g_game_post_frost_color[1] = g > 0.0f ? g : 0.0f;
    g_game_post_frost_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_portal_vortex3d(int32_t enabled,
    float swirl_turns, float phase, float strength, float r, float g, float b) {
    g_game_portal_vortex_params[0] = enabled ? 1.0f : 0.0f;
    g_game_portal_vortex_params[1] = swirl_turns;
    g_game_portal_vortex_params[2] = phase;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_portal_vortex_params[3] = strength;
    g_game_portal_vortex_color[0] = r > 0.0f ? r : 0.0f;
    g_game_portal_vortex_color[1] = g > 0.0f ? g : 0.0f;
    g_game_portal_vortex_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_sepia_vintage3d(float strength,
    float scratch_amount, float seed) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (scratch_amount < 0.0f) scratch_amount = 0.0f;
    if (scratch_amount > 1.0f) scratch_amount = 1.0f;
    g_game_post_sepia[0] = strength;
    g_game_post_sepia[1] = scratch_amount;
    g_game_post_sepia[2] = seed;
}

ZAN_GAME_API void zan_game_set_blueprint_wire3d(int32_t enabled,
    float grid_density, float line_width, float strength,
    float r, float g, float b) {
    g_game_blueprint_wire_params[0] = enabled ? 1.0f : 0.0f;
    g_game_blueprint_wire_params[1] = grid_density > 0.25f ? grid_density : 2.0f;
    if (line_width < 0.01f) line_width = 0.01f;
    if (line_width > 0.25f) line_width = 0.25f;
    g_game_blueprint_wire_params[2] = line_width;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_blueprint_wire_params[3] = strength;
    g_game_blueprint_wire_color[0] = r > 0.0f ? r : 0.0f;
    g_game_blueprint_wire_color[1] = g > 0.0f ? g : 0.0f;
    g_game_blueprint_wire_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_sun_bloom_streak3d(float strength,
    float threshold, float r, float g, float b) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 2.0f) threshold = 2.0f;
    g_game_post_streak_params[0] = strength;
    g_game_post_streak_params[1] = threshold;
    g_game_post_streak_color[0] = r > 0.0f ? r : 0.0f;
    g_game_post_streak_color[1] = g > 0.0f ? g : 0.0f;
    g_game_post_streak_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_heat_haze_shimmer3d(int32_t enabled,
    float freq, float phase, float strength, float r, float g, float b) {
    g_game_heat_haze_params[0] = enabled ? 1.0f : 0.0f;
    g_game_heat_haze_params[1] = freq > 0.5f ? freq : 4.0f;
    g_game_heat_haze_params[2] = phase;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_heat_haze_params[3] = strength;
    g_game_heat_haze_color[0] = r > 0.0f ? r : 0.0f;
    g_game_heat_haze_color[1] = g > 0.0f ? g : 0.0f;
    g_game_heat_haze_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_split_tone3d(float strength, float balance,
    float shadow_r, float shadow_g, float shadow_b,
    float high_r, float high_g, float high_b) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (balance < -1.0f) balance = -1.0f;
    if (balance > 1.0f) balance = 1.0f;
    g_game_post_split_params[0] = strength;
    g_game_post_split_params[1] = balance;
    g_game_post_split_shadow[0] = shadow_r > 0.0f ? shadow_r : 0.0f;
    g_game_post_split_shadow[1] = shadow_g > 0.0f ? shadow_g : 0.0f;
    g_game_post_split_shadow[2] = shadow_b > 0.0f ? shadow_b : 0.0f;
    g_game_post_split_high[0] = high_r > 0.0f ? high_r : 0.0f;
    g_game_post_split_high[1] = high_g > 0.0f ? high_g : 0.0f;
    g_game_post_split_high[2] = high_b > 0.0f ? high_b : 0.0f;
}

ZAN_GAME_API void zan_game_set_pixel_dither_fade3d(int32_t enabled,
    float opacity, float pixel_scale) {
    g_game_pixel_dither_params[0] = enabled ? 1.0f : 0.0f;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    g_game_pixel_dither_params[1] = opacity;
    if (pixel_scale < 1.0f) pixel_scale = 1.0f;
    if (pixel_scale > 8.0f) pixel_scale = 8.0f;
    g_game_pixel_dither_params[2] = pixel_scale;
}

ZAN_GAME_API void zan_game_set_post_oil_paint3d(float strength, float radius) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (radius < 1.0f) radius = 1.0f;
    if (radius > 4.0f) radius = 4.0f;
    g_game_post_oil_paint[0] = strength;
    g_game_post_oil_paint[1] = radius;
}

ZAN_GAME_API void zan_game_set_crosshatch_ink3d(int32_t enabled, float density,
    float ink_strength, float r, float g, float b) {
    g_game_crosshatch_params[0] = enabled ? 1.0f : 0.0f;
    g_game_crosshatch_params[1] = density > 2.0f ? density : 8.0f;
    if (ink_strength < 0.0f) ink_strength = 0.0f;
    if (ink_strength > 1.0f) ink_strength = 1.0f;
    g_game_crosshatch_params[2] = ink_strength;
    g_game_crosshatch_color[0] = r > 0.0f ? r : 0.0f;
    g_game_crosshatch_color[1] = g > 0.0f ? g : 0.0f;
    g_game_crosshatch_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_scope_edge_blur3d(float strength,
    float clear_radius) {
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (clear_radius < 0.1f) clear_radius = 0.1f;
    if (clear_radius > 1.0f) clear_radius = 1.0f;
    g_game_post_scope_blur[0] = strength;
    g_game_post_scope_blur[1] = clear_radius;
}

ZAN_GAME_API void zan_game_set_aniso_brushed3d(int32_t enabled,
    float grain_freq, float anisotropy, float strength,
    float r, float g, float b) {
    g_game_aniso_brushed_params[0] = enabled ? 1.0f : 0.0f;
    g_game_aniso_brushed_params[1] = grain_freq > 1.0f ? grain_freq : 12.0f;
    if (anisotropy < 2.0f) anisotropy = 2.0f;
    if (anisotropy > 64.0f) anisotropy = 64.0f;
    g_game_aniso_brushed_params[2] = anisotropy;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 3.0f) strength = 3.0f;
    g_game_aniso_brushed_params[3] = strength;
    g_game_aniso_brushed_color[0] = r > 0.0f ? r : 0.0f;
    g_game_aniso_brushed_color[1] = g > 0.0f ? g : 0.0f;
    g_game_aniso_brushed_color[2] = b > 0.0f ? b : 0.0f;
}

ZAN_GAME_API void zan_game_set_post_daltonize3d(float mode, float strength) {
    if (mode < 0.0f) mode = 0.0f;
    if (mode > 3.0f) mode = 3.0f;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    g_game_post_daltonize[0] = mode;
    g_game_post_daltonize[1] = strength;
}

