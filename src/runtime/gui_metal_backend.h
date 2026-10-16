#ifndef ZAN_GUI_METAL_BACKEND_H
#define ZAN_GUI_METAL_BACKEND_H

#include "gui_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 检测当前平台是否支持 Metal 硬件管线 (由纯 Zan gui_metal_backend.zan 实现并导出) */
int zan_gui_internal_metal_supported(void);

/* 初始化并挂接 Metal 渲染后端 (由纯 Zan gui_metal_backend.zan 实现并导出) */
int zan_gui_internal_metal_install(void);

/* 释放 Metal 上下文与 GPU 资源 (由纯 Zan gui_metal_backend.zan 实现并导出) */
void zan_gui_internal_metal_drop_present(void);

/* Metal 渲染后端：对接纯 Zan 硬件加速后端，底层图形指令优雅降级到软光栅 */
static const zan_gui_backend zan_metal_backend = {
    .name              = "metal",
    .clear_rect        = cpu_clear_rect,
    .fill_rect         = cpu_fill_rect,
    .fill_round        = cpu_fill_round,
    .draw_round        = cpu_draw_round,
    .surface_round     = cpu_surface_round,
    .fill_vgrad        = cpu_fill_vgrad,
    .fill_grad         = cpu_fill_grad,
    .shadow_round      = cpu_shadow_round,
    .fill_circle       = cpu_fill_circle,
    .draw_circle       = cpu_draw_circle,
    .fill_radial       = cpu_fill_radial,
    .fill_sector       = cpu_fill_sector,
    .draw_line         = cpu_draw_line,
    .polyline          = cpu_polyline,
    .blur              = cpu_blur,
    .snapshot          = cpu_snapshot,
    .snapshot_patch    = cpu_snapshot_patch,
    .restore           = cpu_restore,
    .draw_text         = NULL,
    .glyph_run         = cpu_glyph_run,
    .blit_pixels       = cpu_blit_pixels,
    .blit_pixels_batch = cpu_blit_pixels_batch,
    .set_clip          = NULL,
    .flush             = NULL,
    .read_pixels       = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* ZAN_GUI_METAL_BACKEND_H */
