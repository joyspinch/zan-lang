#ifndef ZAN_GUI_METAL_BACKEND_H
#define ZAN_GUI_METAL_BACKEND_H

#include "gui_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 检测当前 Apple 平台是否支持 Metal 硬件管线 */
int zan_gui_internal_metal_supported(void);

/* 初始化并挂接 Metal 渲染后端 */
int zan_gui_internal_metal_install(void);

/* 释放 Metal 上下文与 GPU 资源 */
void zan_gui_internal_metal_drop_present(void);

/* Metal 渲染后端虚表导出 */
extern const zan_gui_backend zan_metal_backend;

#ifdef __cplusplus
}
#endif

#endif /* ZAN_GUI_METAL_BACKEND_H */
