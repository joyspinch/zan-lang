#ifndef ZAN_GUI_GRAPHICS_H
#define ZAN_GUI_GRAPHICS_H

#include "zan_bitmap.h"
#include "gui_gl.h"

#ifndef ZAN_GUI_GRAPHICS_API
#define ZAN_GUI_GRAPHICS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 底层系统交互与数据协议契约 */
ZAN_GUI_GRAPHICS_API const zan_bitmap *zan_gui_surface_bitmap(int32_t surface_id);
ZAN_GUI_GRAPHICS_API void zan_gui_blit_pixels(int32_t surface_id,
    const zan_bitmap *bitmap, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh);
/* 底层系统交互与数据协议契约 */
ZAN_GUI_GRAPHICS_API void zan_gui_blit_pixels_batch(int32_t surface_id,
    const zan_bitmap *bitmap, const float *quads, int32_t count);
ZAN_GUI_GRAPHICS_API void zan_gui_blit_surface_scaled(int32_t dst_id,
    int32_t src_id, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh);

typedef struct zan_gpu_frame {
    const zan_gl_api *api;
    uint32_t framebuffer;
    int width, height;
    int clip_x, clip_y, clip_w, clip_h;
    uint64_t epoch;
} zan_gpu_frame;

/* 底层系统交互与数据协议契约 */
ZAN_GUI_GRAPHICS_API int32_t zan_gui_gpu_begin(int32_t surface_id,
    zan_gpu_frame *frame);
ZAN_GUI_GRAPHICS_API void zan_gui_gpu_end(int32_t surface_id);
ZAN_GUI_GRAPHICS_API uint32_t zan_gui_gpu_program(const char *vertex,
    const char *fragment);
ZAN_GUI_GRAPHICS_API uint32_t zan_gui_gpu_texture(const zan_bitmap *bitmap);
/* 底层系统交互与数据协议契约 */
ZAN_GUI_GRAPHICS_API int32_t zan_gui_gpu_register_cleanup(
    void (*cleanup)(int32_t surface_id));

#ifdef __cplusplus
}
#endif
#endif
