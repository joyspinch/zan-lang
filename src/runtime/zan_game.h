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
 * 与 uColor、纹理相乘。返回网格 id，0 = 失败/后端不支持。 */
ZAN_GAME_API int32_t zan_game_mesh_create_ex(int32_t surface_id,
    const float *verts, int32_t count, const unsigned short *indices,
    int32_t index_count, int32_t flags);
ZAN_GAME_API int32_t zan_game_draw3d(int32_t surface_id, int32_t mesh,
    const float *mvp, int32_t color, const char *texture);

/* 批量实例绘制：begin（清一次深度）→ 每网格一组 batch（instances 每实例
 * 20 float：mvp 16 列主序 + tint rgba 4）→ end（恢复状态，整批一次
 * Finish）。批内实例与跨网格组之间做深度解析。返回 1 = 已画，
 * 0 = 失败/CPU 兜底档。 */
ZAN_GAME_API int32_t zan_game_draw3d_batch_begin(int32_t surface_id);
ZAN_GAME_API int32_t zan_game_draw3d_batch(int32_t surface_id, int32_t mesh,
    const char *texture, const float *instances, int32_t count);
ZAN_GAME_API int32_t zan_game_draw3d_batch_end(int32_t surface_id);

#ifdef __cplusplus
}
#endif

#endif
