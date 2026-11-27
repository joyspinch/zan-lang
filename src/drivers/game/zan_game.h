#ifndef ZAN_GAME_H
#define ZAN_GAME_H

#include <stdint.h>

#if defined(ZAN_GAME_STATIC)
#define ZAN_GAME_API
#elif defined(_WIN32)
#define ZAN_GAME_API __declspec(dllexport)
#else
#define ZAN_GAME_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

ZAN_GAME_API int32_t zan_game_sprite_handle(const char *key);
ZAN_GAME_API int32_t zan_game_bake_sprite(const char *key, int32_t surface_id,
    int32_t x, int32_t y, int32_t width, int32_t height);
ZAN_GAME_API void zan_game_sprite_batch(int32_t surface_id, int32_t handle,
    const float *quads, int32_t count);

ZAN_GAME_API int32_t zan_game_mesh_create(int32_t surface_id, const float *verts,
    int32_t count, const unsigned short *indices, int32_t index_count);
/* flags bit0 = colored：顶点 12 float（pos3+normal3+uv2+color4），颜色
 * 与 uColor、纹理相乘。
 * flags bit1 = skinned：顶点 16 float（pos3+normal3+uv2+joints4+weights4），
 * 用于 GPU 骨骼蒙皮绘制（zan_game_draw3d_skinned）。
 * 返回网格 id，0 = 失败/后端不支持。 */
ZAN_GAME_API int32_t zan_game_mesh_create_ex(int32_t surface_id,
    const float *verts, int32_t count, const unsigned short *indices,
    int32_t index_count, int32_t flags);
ZAN_GAME_API int32_t zan_game_draw3d(int32_t surface_id, int32_t mesh,
    const float *mvp, int32_t color, const char *texture);
/* 扩展 3D 绘制：额外传入模型世界矩阵 model（16 float 列主序，用于阴影映射
 * 坐标变换），若已激活 ShadowMap 则自动采样 3x3 PCF 软阴影。 */
ZAN_GAME_API int32_t zan_game_draw3d_ex(int32_t surface_id, int32_t mesh,
    const float *mvp, const float *model, int32_t color, const char *texture);

/* Alpha/Additive 混合与深度写入状态控制（应用于后续 draw3d / draw3d_ex /
 * draw3d_skinned / draw3d_batch）。
 * enabled: 0 = 关闭混合（不透明），1 = 标准 Alpha 混合（SRC_ALPHA, ONE_MINUS_SRC_ALPHA），
 *          2 = 加法辉光混合（SRC_ALPHA, ONE，用于火焰/剑光/能量拖尾 VFX）。
 * 默认 enabled=0, depth_write=1。 */
ZAN_GAME_API int32_t zan_game_set_blend3d(int32_t enabled, int32_t depth_write);

/* 硬件面剔除状态控制（已自动适配 Canvas FBO 顶下 Y 翻转绕序，标准 CCW 正面保持为正面）：
 * mode: 0 = 关闭剔除（双面 DoubleSided），1 = 剔除背面（Back，闭合实体 ~2x 像素填充提速），
 *       2 = 剔除正面（Front）。默认 mode=0。 */
ZAN_GAME_API int32_t zan_game_set_cull3d(int32_t mode);

/* 多 Pass 3D 帧深度缓冲复用边界：begin_frame3d 清一次深度缓冲并开启跨调用
 * 深度保留（不透明静态 + 蒙皮角色 + 透明层共享同一 24-bit 深度缓冲）；
 * end_frame3d 恢复单次 draw3d 自动清深度的默认兼容行为。 */
ZAN_GAME_API int32_t zan_game_begin_frame3d(int32_t surface_id);
ZAN_GAME_API void zan_game_end_frame3d(void);

/* 单方向光 ShadowMap 渲染 Pass（PCF 3x3 软阴影）：
 * shadow_begin（指定分辨率 256..4096 与光源 ViewProjection 矩阵）→
 * 多次 shadow_draw（投射体网格 + model 矩阵）→ shadow_end（激活主 Pass
 * 阴影采样）。shadow_clear 关闭阴影采样。 */
ZAN_GAME_API int32_t zan_game_shadow_begin(int32_t surface_id, int32_t map_size,
    const float *light_vp);
ZAN_GAME_API int32_t zan_game_shadow_draw(int32_t surface_id, int32_t mesh,
    const float *model);
/* 蒙皮网格动态阴影投射（使用当前 zan_game_set_bones3d 上传的骨骼矩阵变形后写入 ShadowMap）。 */
ZAN_GAME_API int32_t zan_game_shadow_draw_skinned(int32_t surface_id, int32_t mesh,
    const float *model);
ZAN_GAME_API int32_t zan_game_shadow_end(int32_t surface_id);
ZAN_GAME_API void zan_game_shadow_clear(void);

/* GPU 骨骼蒙皮通路（最多 64 根骨骼，列主序 mat4）：
 * 先通过 zan_game_set_bones3d 上传当前帧骨骼蒙皮矩阵数组，再用
 * zan_game_draw3d_skinned 绘制 flags bit1=1 的蒙皮网格。 */
ZAN_GAME_API int32_t zan_game_set_bones3d(int32_t count, const float *matrices16);
ZAN_GAME_API int32_t zan_game_draw3d_skinned(int32_t surface_id, int32_t mesh,
    const float *mvp, const float *model, int32_t color, const char *texture);

/* 批量实例绘制：begin（清一次深度）→ 每网格一组 batch（instances 每实例
 * 20 float：mvp 16 列主序 + tint rgba 4）→ end（恢复状态，整批一次
 * Finish）。批内实例与跨网格组之间做深度解析。返回 1 = 已画，
 * 0 = 失败/CPU 兜底档。 */
ZAN_GAME_API int32_t zan_game_draw3d_batch_begin(int32_t surface_id);
ZAN_GAME_API int32_t zan_game_draw3d_batch(int32_t surface_id, int32_t mesh,
    const char *texture, const float *instances, int32_t count);
ZAN_GAME_API int32_t zan_game_draw3d_batch_end(int32_t surface_id);

/* 3D 光照 uniform 通路（draw3d 与批量实例共用，A489）：count 为方向光
 * 数量（0..4），dirs/colors 各 count*3 个 float——指向光源的方向（与
 * 网格法线同空间，着色器内归一化）与 rgb 颜色贡献；ambient 为 3 个
 * float（可 NULL 保留当前值）。缺省 = 一盏键光 dir(0.4,0.8,0.6)
 * color 0.45 + ambient 0.55（半兰伯特），与历史固定光照逐位一致。
 * 返回 1 = 已生效，0 = 参数非法。 */
ZAN_GAME_API int32_t zan_game_set_lights3d(int32_t count, const float *dirs,
    const float *colors, const float *ambient);

/* 商业级 3D 相机、PBR 材质/自发光、动态点光源与线性距离雾状态（用于 draw3d_ex /
 * draw3d_skinned；缺省值保证历史纯漫反射着色逐位兼容）：
 * - set_camera3d: 传入世界空间相机位置（激活高光视角与距离雾计算）。
 * - set_material3d: metallic [0..1], roughness [0..1], emissive RGB（自发光不受光照与阴影衰减）。
 * - set_point_lights3d: 最多 4 盏动态点光源，pos_range4 为 count*4 float [x,y,z,radius]，
 *   colors3 为 count*3 float [r,g,b]。
 * - set_fog3d: 线性大气距离雾（start..end 区间平滑混入雾色）。 */
ZAN_GAME_API void zan_game_set_camera3d(float x, float y, float z);
ZAN_GAME_API void zan_game_set_material3d(float metallic, float roughness,
    float em_r, float em_g, float em_b);
ZAN_GAME_API int32_t zan_game_set_point_lights3d(int32_t count,
    const float *pos_range4, const float *colors3);
ZAN_GAME_API void zan_game_set_fog3d(int32_t enabled, float r, float g, float b,
    float start_dist, float end_dist);

/* 屏幕空间 GPU 后处理 Pass（Exposure 曝光 + 13-tap 亮部 Bloom 泛光 +
 * Reinhard / ACES Filmic 色调映射 + 径向 Vignette 暗角）：
 * 通过离屏颜色纹理 BlitFramebuffer 读写分离，保留原 Alpha 通道。
 * tonemap_mode: 0 = None/Clamp, 1 = Reinhard, 2 = ACES Filmic。 */
ZAN_GAME_API int32_t zan_game_postprocess3d(int32_t surface_id, float exposure,
    int32_t tonemap_mode, float vignette, float bloom_threshold,
    float bloom_intensity);

/* 切线空间法线贴图状态（纹理单元 2，着色器内通过屏幕空间偏导 dFdx/dFdy 构建
 * 余切帧，零破坏兼容现有 8/12/16 float 静态与蒙皮网格）。
 * texture 为空或 scale <= 0 时关闭法线贴图（零采样开销）。 */
ZAN_GAME_API void zan_game_set_normal_map3d(const char *texture, float scale);

/* 程序化 GPU 大气天空盒 Pass（全屏无顶点缓冲三角形，关闭深度写入）：
 * cam_basis9 = [right.xyz, up.xyz, forward.xyz]（世界空间相机正交基），
 * tan_half_fov = tan(fovY * 0.5)，aspect = width / height，
 * sun_dir3 = 指向太阳的世界单位方向，top/horizon/ground_rgb3 = 线性 0..1 颜色。 */
ZAN_GAME_API int32_t zan_game_skybox3d(int32_t surface_id,
    const float *cam_basis9, float tan_half_fov, float aspect,
    const float *sun_dir3, const float *top_rgb3, const float *horizon_rgb3,
    const float *ground_rgb3);

/* 动态 GPU 聚光灯（最多 4 盏，支持内外锥角平滑衰减 + 距离衰减 + Blinn-Phong 高光）：
 * - pos_range4: count*4 float [pos.x, pos.y, pos.z, range]
 * - dir_inner4: count*4 float [dir.x, dir.y, dir.z, cos(innerAngle)]（dir 为光线射出方向）
 * - color_outer4: count*4 float [r, g, b, cos(outerAngle)] */
ZAN_GAME_API int32_t zan_game_set_spot_lights3d(int32_t count,
    const float *pos_range4, const float *dir_inner4, const float *color_outer4);

/* 半球天空/地面环境光 IBL：按法线 N.y 混合天空顶色与地面反弹色，替代平淡单一环境光；
 * enabled=0 时保持历史 uAmbient 行为完全不变。 */
ZAN_GAME_API void zan_game_set_hemi_ambient3d(int32_t enabled,
    float sky_r, float sky_g, float sky_b,
    float ground_r, float ground_g, float ground_b);

/* glTF 2.0 标准 PBR ORM 复合贴图（纹理单元 3：R=AO 环境光遮蔽, G=Roughness 粗糙度,
 * B=Metallic 金属度，与材质标量相乘；ao_strength 控制 AO 对环境光的遮蔽强度 [0..1]）。
 * orm_texture 为空时关闭采样（零开销）。 */
ZAN_GAME_API void zan_game_set_pbr_maps3d(const char *orm_texture,
    float ao_strength);

/* Alpha Cutout 裁切阈值（glTF 2.0 alphaMode="MASK"）：当 cutoff > 0 且片元最终
 * alpha < cutoff 时执行 discard（跳过颜色与深度写入，用于树叶/栅栏/毛发卡片）；
 * cutoff <= 0 关闭裁切。 */
ZAN_GAME_API void zan_game_set_alpha_cutoff3d(float cutoff);

#ifdef __cplusplus
}
#endif

#endif
