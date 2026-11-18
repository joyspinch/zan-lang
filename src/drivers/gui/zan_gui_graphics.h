#ifndef ZAN_GUI_GRAPHICS_H
#define ZAN_GUI_GRAPHICS_H

#include "../image/zan_bitmap.h"
#include "gui_gl.h"

#ifndef ZAN_GUI_GRAPHICS_API
#define ZAN_GUI_GRAPHICS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

ZAN_GUI_GRAPHICS_API const zan_bitmap *zan_gui_surface_bitmap(int32_t surface_id);
ZAN_GUI_GRAPHICS_API void zan_gui_blit_pixels(int32_t surface_id,
    const zan_bitmap *bitmap, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh);
ZAN_GUI_GRAPHICS_API void zan_gui_blit_pixels_batch(int32_t surface_id,
    const zan_bitmap *bitmap, const float *quads, int32_t count);
ZAN_GUI_GRAPHICS_API void zan_gui_blit_surface_scaled(int32_t dst_id,
    int32_t src_id, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh);

/* Internal shell bulk conversion. Strides are positive byte counts; src is
   straight ARGB32, dst is premultiplied ARGB32, with no overlapping buffers.
   The mask, when non-NULL, covers src_width x src_height with its own stride
   and byte length; a NULL mask means full coverage. Shape/DPI sampling stays
   with the caller. Source is placed at (offset_x, offset_y), clipped to dst;
   every other dst pixel becomes transparent, while row padding is untouched.
   Buffers must contain their declared extents and ARGB rows must be 4-byte
   aligned. Returns 0 without writing for invalid extents/strides/mask length
   or overlap, otherwise 1 (an empty source clears dst). */
ZAN_GUI_GRAPHICS_API int32_t zan_gui_prepare_layered_argb(void *dst,
    int32_t dst_width, int32_t dst_height, int32_t dst_stride,
    const void *src, int32_t src_width, int32_t src_height, int32_t src_stride,
    int32_t offset_x, int32_t offset_y, const uint8_t *coverage,
    int32_t coverage_stride, int32_t coverage_length, int32_t shadow_band);

/* Internal initial background layer. Same coverage/straight source-over result
   as fill_rounded_rect, but transparent destination pixels are assigned directly.
   Nontransparent pixels and non-CPU backends retain ordinary source-over. */
ZAN_GUI_GRAPHICS_API void zan_gui_fill_initial_shape(int32_t surface_id,
    int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, int32_t color);

typedef struct zan_gpu_frame {
    const zan_gl_api *api;
    uint32_t framebuffer;
    int width, height;
    int clip_x, clip_y, clip_w, clip_h;
    uint64_t epoch;
} zan_gpu_frame;

ZAN_GUI_GRAPHICS_API int32_t zan_gui_gpu_begin(int32_t surface_id,
    zan_gpu_frame *frame);
ZAN_GUI_GRAPHICS_API void zan_gui_gpu_end(int32_t surface_id);
ZAN_GUI_GRAPHICS_API uint32_t zan_gui_gpu_program(const char *vertex,
    const char *fragment);
ZAN_GUI_GRAPHICS_API uint32_t zan_gui_gpu_texture(const zan_bitmap *bitmap);
ZAN_GUI_GRAPHICS_API int32_t zan_gui_gpu_register_cleanup(
    void (*cleanup)(int32_t surface_id));

#ifdef __cplusplus
}
#endif
#endif
