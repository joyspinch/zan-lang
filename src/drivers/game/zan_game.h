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

/* glTF 2.0 自发光贴图（纹理单元 4）：片元自发光 = uEmissive * texture(uEmissiveMap, v_uv).rgb，
 * 用于局部发光纹理（霓虹/符文/角色眼部/仪表盘）并可直接驱动 Bloom 泛光；texture 为空关闭采样。 */
ZAN_GAME_API void zan_game_set_emissive_map3d(const char *texture);

/* GPU UV 平铺与滚动变换（KHR_texture_transform / 纹理序列帧 Flipbook / 流水特效）：
 * 顶点着色器输出 v_uv = a_uv * vec2(scale_u, scale_v) + vec2(offset_u, offset_v)。
 * 缺省 (1, 1, 0, 0) 为恒等变换。 */
ZAN_GAME_API void zan_game_set_uv_transform3d(float scale_u, float scale_v,
    float offset_u, float offset_v);

/* GPU 菲涅尔轮廓边缘光（Rim Light，用于 3D 动作/二次元角色轮廓高光与受击闪白）：
 * rgb 为边缘光线性颜色（全 0 关闭，零开销），power 为锐度指数（推荐 2.0..5.0）。 */
ZAN_GAME_API void zan_game_set_rim_light3d(float r, float g, float b, float power);

/* GPU 后处理调色参数（在 zan_game_postprocess3d 色调映射后、暗角前生效，零额外 Pass）：
 * saturation（0=黑白, 1=原色, >1 增饱和）、contrast（1=原对比度）、tint RGB（1,1,1=无偏色）。 */
ZAN_GAME_API void zan_game_set_color_grade3d(float saturation, float contrast,
    float tint_r, float tint_g, float tint_b);

/* GPU 风格化卡通/二次元色阶量化（Toon / Cel Shading）：
 * bands >= 2.0 开启漫反射色阶量化（推荐 3..5 阶），< 2.0 关闭还原 PBR 连续着色；
 * specular_step 控制高光硬边阈值（推荐 0.25..0.55）。 */
ZAN_GAME_API void zan_game_set_toon_shading3d(float bands, float specular_step);

/* GPU 后处理 FXAA 抗锯齿与径向色差镜头特效（零额外 Pass，在 zan_game_postprocess3d 中生效）：
 * fxaa_strength [0..1] 控制边缘亮度抗锯齿强度；chromatic_strength [0..0.1] 控制径向红蓝分离强度。 */
ZAN_GAME_API void zan_game_set_post_lens3d(float fxaa_strength, float chromatic_strength);

/* GPU 程序化噪声消融特效（Dissolve Shader，零额外贴图）：
 * threshold [0..1.1] 控制噪声丢弃阈值（<=0 关闭，>=1.05 全消融）；
 * edge_width 控制燃烧发光边缘宽度，edge_r/g/b 为边缘发光线性颜色（可联动 Bloom）。 */
ZAN_GAME_API void zan_game_set_dissolve3d(float threshold, float edge_width,
    float edge_r, float edge_g, float edge_b);

/* GPU 世界空间任意裁剪平面（Clip Plane，用于水面平面反射、传送门、剖面视图、斩击切面）：
 * enabled != 0 时丢弃满足 dot(worldPos, (nx,ny,nz)) + d < 0 的片元。 */
ZAN_GAME_API void zan_game_set_clip_plane3d(int32_t enabled,
    float nx, float ny, float nz, float d);

/* GPU 世界空间三平面纹理投影（Triplanar Mapping，消除陡坡/悬崖/体素/程序化地形的 UV 拉伸）：
 * enabled != 0 时按世界空间坐标与法线权重三轴采样 uTex 并以 fract 无缝平铺；
 * scale 为世界每米平铺频率，sharpness 为轴向混合锐度（推荐 2.0..8.0）。 */
ZAN_GAME_API void zan_game_set_triplanar3d(int32_t enabled, float scale,
    float sharpness);

/* GPU 指数高度大气雾（Exponential Height Fog，山谷薄雾/地底毒雾/云海分层氛围）：
 * enabled != 0 时按 exp(-max(worldY - base_y, 0) * falloff) * max_density 混入雾色。 */
ZAN_GAME_API void zan_game_set_height_fog3d(int32_t enabled, float base_y,
    float falloff, float max_density, float r, float g, float b);

/* GPU 受击闪白与异常状态覆色（Hit-Flash / Status Overlay，动作/RPG 战斗反馈）：
 * amount [0..1] 按 mix(rgb, (r,g,b), amount) 在光照后、雾效前叠色（0 关闭，零开销）。 */
ZAN_GAME_API void zan_game_set_hit_flash3d(float r, float g, float b, float amount);

/* GPU 后处理锐化与胶片颗粒（Unsharp-Mask Sharpen + Temporal Film Grain，零额外 Pass）：
 * sharpen_strength [0..2] 增强边缘清晰度；grain_strength [0..0.5] 叠加防色带胶片噪点，grain_seed 驱动逐帧变化。 */
ZAN_GAME_API void zan_game_set_post_film3d(float sharpen_strength,
    float grain_strength, float grain_seed);

/* GPU 全息扫描线与能量护盾菲涅尔覆层（Hologram Scanlines + Fresnel Energy Shield）：
 * enabled != 0 时按世界 Y 轴正弦波扫描线叠加能量发光色（freq 频率、phase 滚动相位、strength 强度）。 */
ZAN_GAME_API void zan_game_set_holo_scan3d(int32_t enabled, float freq,
    float phase, float strength, float r, float g, float b);

/* GPU 电影宽银幕黑边与水下/热浪屏幕波纹畸变（Cinema Letterbox + Wave Distortion，零额外 Pass）：
 * letterbox_ratio [0..0.45] 控制上下黑边占比；distort_amp [0..0.08] 控制横向正弦波畸变幅度。 */
ZAN_GAME_API void zan_game_set_post_cinema3d(float letterbox_ratio,
    float distort_amp, float distort_freq, float distort_phase);

/* GPU 世界法线朝上积雪/青苔/落灰材质覆层（World-Aligned Top-Down Snow/Moss Cover）：
 * enabled != 0 时按 smoothstep(threshold, threshold + sharpness, worldNormal.y) * amount 混入顶面覆色并参与光照与阴影。 */
ZAN_GAME_API void zan_game_set_top_cover3d(int32_t enabled, float threshold,
    float sharpness, float amount, float r, float g, float b);

/* GPU 后处理径向冲刺/受击缩放模糊（Radial Speed/Zoom Blur，单 Pass 5 采样零额外 Pass）：
 * strength [0..0.15] 控制模糊拖尾幅度，(center_x, center_y) 为屏幕 UV 聚焦中心 [0..1]。 */
ZAN_GAME_API void zan_game_set_post_radial_blur3d(float strength,
    float center_x, float center_y);

/* GPU 顶点级微风摇曳动画（Vegetation / Flag Wind Sway，零骨骼 GPU 顶点偏移并同步阴影 Pass）：
 * enabled != 0 且 amplitude > 0 时，按局部高度 max(pos.y, 0) * height_scale 沿 XZ 轴施加正弦相位摇曳。 */
ZAN_GAME_API void zan_game_set_wind_sway3d(int32_t enabled, float amplitude,
    float freq, float phase, float height_scale);

/* GPU 后处理复古像素化马赛克与色阶量化（Pixelate Mosaic + Posterize，零额外 Pass）：
 * pixel_size >= 1.5 按指定像素块尺寸量化采样 UV；posterize_levels >= 2 将输出色阶量化为指定级数。 */
ZAN_GAME_API void zan_game_set_post_pixelate3d(float pixel_size,
    float posterize_levels);

/* GPU 屏幕空间 Bayer 4x4 抖动距离淡出（Screen-Door Dithered Alpha Fade，零透明排序开销）：
 * enabled != 0 时按相机距离在 [start_dist, end_dist] 间做 4x4 有序抖动剔除；
 * start_dist < end_dist 用于近距相机防遮挡淡出，start_dist > end_dist 用于远距 LOD 淡出。 */
ZAN_GAME_API void zan_game_set_distance_fade3d(int32_t enabled,
    float start_dist, float end_dist);

/* GPU 后处理双色调渐变映射（Duotone / Night-Vision / Thermal / Sepia Gradient Map，零额外 Pass）：
 * strength [0..1] 按屏幕亮度在 (dark_r,g,b) 与 (light_r,g,b) 间线性插值并混入原画面。 */
ZAN_GAME_API void zan_game_set_post_duotone3d(float strength,
    float dark_r, float dark_g, float dark_b,
    float light_r, float light_g, float light_b);

/* GPU 世界空间抗锯齿程序化网格（World-Space Procedural Grid，适用于战棋/RTS/模拟建造/场景编辑器）：
 * enabled != 0 时按 cell_size 与 fwidth 硬件导数抗锯齿绘制 XZ 平面网格线并按 strength 混入输出。 */
ZAN_GAME_API void zan_game_set_world_grid3d(int32_t enabled, float cell_size,
    float line_width, float strength, float r, float g, float b);

/* GPU 后处理复古 CRT 扫描线与曲面桶形畸变（CRT Scanlines + Barrel Curvature，零额外 Pass）：
 * scanline_intensity [0..0.95] 控制扫描线压暗深度，scanline_count 控制纵向条纹周期，
 * curvature [0..0.5] 控制桶形曲面弯曲与黑边，glow_boost 控制扫描线亮度补偿。 */
ZAN_GAME_API void zan_game_set_post_crt_scanline3d(float scanline_intensity,
    float scanline_count, float curvature, float glow_boost);

/* GPU 世界空间程序化水底焦散光斑（Water Caustics，仅投射在朝上表面，零贴图零额外 Pass）：
 * enabled != 0 时按世界 XZ 坐标双层旋转正弦干涉叠加焦散光网，strength [0..2] 控制亮度。 */
ZAN_GAME_API void zan_game_set_water_caustics3d(int32_t enabled, float scale,
    float phase, float strength, float r, float g, float b);

/* GPU 后处理屏幕空间 Sobel 边缘描边（Screen-Space Edge Outline，二次元/战术轮廓线，零额外 Pass）：
 * strength [0..1] 控制描边混入强度，threshold [0.02..1] 控制亮度梯度边缘阈值，(r,g,b) 为描边色。 */
ZAN_GAME_API void zan_game_set_post_edge_outline3d(float strength,
    float threshold, float r, float g, float b);

/* GPU 世界空间冰霜晶面闪烁与菲涅尔冰膜覆层（Ice/Frost Glint，零贴图零额外 Pass）：
 * enabled != 0 时按视角菲涅尔与世界空间晶面叠加冷霜闪烁光效，glint_strength [0..2]。 */
ZAN_GAME_API void zan_game_set_ice_frost3d(int32_t enabled, float scale,
    float glint_strength, float fresnel_power, float r, float g, float b);

/* GPU 后处理红外热成像伪彩视图（Thermal/IR Heat Vision，五段铁红光谱色带，单 Pass 零额外 FBO）：
 * strength [0..1] 控制混入强度，sensitivity 控制亮度放大系数，cold_shift [-0.5..0.5] 控制冷端偏移。 */
ZAN_GAME_API void zan_game_set_post_thermal_heat3d(float strength,
    float sensitivity, float cold_shift);

/* GPU 世界空间岩浆裂纹发光覆层（Lava Crust / Cracked Magma，零贴图零额外 Pass）：
 * enabled != 0 时沿世界空间应力裂纹场透出脉动熔岩自发光，crack_width [0.02..0.6]，strength [0..3]。 */
ZAN_GAME_API void zan_game_set_lava_crust3d(int32_t enabled, float scale,
    float phase, float crack_width, float strength, float r, float g, float b);

/* GPU 后处理屏幕边缘受击红晕与全屏闪光弹致盲叠加（Screen Edge Damage + Flashbang，单 Pass 零额外 FBO）：
 * edge_strength [0..1] 仅染色屏幕四周边缘（中心零污染），flash_strength [0..1] 控制全屏致盲混入。 */
ZAN_GAME_API void zan_game_set_post_screen_tint_flash3d(float edge_strength,
    float flash_strength, float r, float g, float b);

/* GPU 薄膜干涉虹彩珠光涂层（Thin-Film Iridescence，零贴图零额外 Pass）：
 * enabled != 0 时按视角法线夹角产生薄膜彩虹干涉色，film_thickness [0.5..6]，strength [0..2]。 */
ZAN_GAME_API void zan_game_set_iridescence3d(int32_t enabled,
    float film_thickness, float phase_shift, float strength);

/* GPU 后处理战术夜视仪（NVG P43 绿磷增益 + 圆形目镜遮罩 + 闪烁噪点，单 Pass 零额外 FBO）：
 * strength [0..1] 控制混入强度，gain [0.5..8] 控制微光增益，noise_amount [0..0.5] 控制噪点幅度。 */
ZAN_GAME_API void zan_game_set_post_night_vision3d(float strength, float gain,
    float noise_amount, float seed);

/* GPU 3D 世界空间球面战术声呐脉冲波前（Sonar/Radar Echo Ring，零贴图零额外 Pass）：
 * enabled != 0 时沿距 (origin_x,origin_y,origin_z) 半径 radius 处渲染高能扫描环及宽度 ring_width 的尾迹。 */
ZAN_GAME_API void zan_game_set_sonar_pulse3d(int32_t enabled,
    float origin_x, float origin_y, float origin_z,
    float radius, float ring_width, float strength, float r, float g, float b);

/* GPU 后处理 45° 半色调漫画网点滤镜（Ben-Day Halftone Print Dots，单 Pass 零额外 FBO）：
 * strength [0..1] 控制混入强度，dot_scale [2..32] 控制像素网点间距，contrast [0.25..3] 控制网点对比度。 */
ZAN_GAME_API void zan_game_set_post_halftone_comic3d(float strength,
    float dot_scale, float contrast);

/* GPU 3D 世界空间科幻六边形能量护盾网格（Hex Force-Field Shield，零贴图零额外 Pass）：
 * enabled != 0 时按世界坐标生成正六边形晶格边界发光与能量脉冲，edge_width [0.02..0.35]。 */
ZAN_GAME_API void zan_game_set_force_field_hex3d(int32_t enabled,
    float scale, float phase, float edge_width, float strength,
    float r, float g, float b);

/* GPU 后处理赛博故障水平撕裂与 RGB 通道分离（Cyberpunk Glitch RGB Split，单 Pass 零额外 FBO）：
 * intensity [0..1] 控制触发密度与位移幅度，slice_count [4..128] 控制水平条带数，seed 控制帧随机种子。 */
ZAN_GAME_API void zan_game_set_post_glitch_rgb3d(float intensity,
    float slice_count, float seed);

/* GPU 3D 世界空间雨滴水洼同心涟漪（Rain Puddle Ripples，仅投射在朝上表面，零贴图零额外 Pass）：
 * enabled != 0 时按世界 XZ 单元格生成周期扩散的雨滴同心高光涟漪，strength [0..3]。 */
ZAN_GAME_API void zan_game_set_rain_ripples3d(int32_t enabled, float scale,
    float phase, float strength, float r, float g, float b);

/* GPU 后处理微缩移轴景深模糊（Tilt-Shift Diorama Blur，单 Pass 零额外 FBO）：
 * strength [0..1] 控制模糊强度，focus_y [0..1] 控制清晰焦带中心高度，focus_band [0.02..0.45] 控制焦带半宽。 */
ZAN_GAME_API void zan_game_set_post_tilt_shift3d(float strength,
    float focus_y, float focus_band);

/* GPU 3D 战术透视轮廓光（X-Ray Silhouette Rim，零贴图零额外 Pass）：
 * enabled != 0 时按视角 Fresnel 轮廓叠加高亮光边，power [0.5..8] 控制边缘锐度，strength [0..3]。 */
ZAN_GAME_API void zan_game_set_xray_silhouette3d(int32_t enabled, float power,
    float strength, float r, float g, float b);

/* GPU 后处理屏幕边缘冰晶霜冻（Edge Frost Vignette，单 Pass 零额外 FBO）：
 * intensity [0..1] 控制冰霜不透明度，radius [0.15..0.95] 控制中心清晰区半径，r/g/b 为冰晶色。 */
ZAN_GAME_API void zan_game_set_post_vignette_frost3d(float intensity,
    float radius, float r, float g, float b);

/* GPU 3D 传送门螺旋能量漩涡（Portal Vortex Swirl，零贴图零额外 Pass）：
 * enabled != 0 时按 UV 极坐标生成带旋臂旋转与径向环罩的传送门能量发光，strength [0..3]。 */
ZAN_GAME_API void zan_game_set_portal_vortex3d(int32_t enabled,
    float swirl_turns, float phase, float strength, float r, float g, float b);

/* GPU 后处理复古怀旧棕褐色调与胶片划痕（Vintage Sepia Film，单 Pass 零额外 FBO）：
 * strength [0..1] 控制怀旧棕褐混合强度，scratch_amount [0..1] 控制竖向老胶片划痕密度，seed 控制帧随机。 */
ZAN_GAME_API void zan_game_set_post_sepia_vintage3d(float strength,
    float scratch_amount, float seed);

#ifdef __cplusplus
}
#endif

#endif
