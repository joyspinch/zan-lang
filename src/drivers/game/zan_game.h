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

/* Alpha 透明混合与深度写入状态控制（应用于后续 draw3d / draw3d_ex /
 * draw3d_skinned / draw3d_batch）。默认 enabled=0, depth_write=1。 */
ZAN_GAME_API int32_t zan_game_set_blend3d(int32_t enabled, int32_t depth_write);

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

#ifdef __cplusplus
}
#endif

#endif
