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

/* 底层系统交互与数据协议契约 */
ZAN_GAME_API int32_t zan_game_sprite_handle(const char *key);
ZAN_GAME_API int32_t zan_game_bake_sprite(const char *key, int32_t surface_id,
    int32_t x, int32_t y, int32_t width, int32_t height);
/* 底层系统交互与数据协议契约 */
ZAN_GAME_API void zan_game_sprite_batch(int32_t surface_id, int32_t handle,
    const float *quads, int32_t count);

/* 底层系统交互与数据协议契约 */
ZAN_GAME_API int32_t zan_game_mesh_create(int32_t surface_id, const float *verts,
    int32_t count, const unsigned short *indices, int32_t index_count);
/* 底层系统交互与数据协议契约 */
ZAN_GAME_API int32_t zan_game_draw3d(int32_t surface_id, int32_t mesh,
    const float *mvp, int32_t color, const char *texture);

#ifdef __cplusplus
}
#endif

#endif
