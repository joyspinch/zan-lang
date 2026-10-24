#ifndef ZAN_IMAGE_H
#define ZAN_IMAGE_H

#include "zan_bitmap.h"

#ifndef ZAN_IMAGE_EXPORT
#define ZAN_IMAGE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 统一原生驱动链接声明；缓存接口由调用方保证并发序列化 */
extern ZAN_IMAGE_EXPORT int32_t zan_image_width(const char *key);
extern ZAN_IMAGE_EXPORT int32_t zan_image_height(const char *key);
extern ZAN_IMAGE_EXPORT void zan_image_evict(const char *key);
extern ZAN_IMAGE_EXPORT const char *zan_image_mem_report(void);

/* 注册或查询内存图像键 (以 "mem:" 为前缀)，成功返回宽度，失败返回 0 */
extern ZAN_IMAGE_EXPORT int32_t zan_image_load_mem(const char *key, const char *data, int32_t len);
extern ZAN_IMAGE_EXPORT int32_t zan_image_load_mem_bytes(const char *key, const char *data,
                                       int32_t len);
extern ZAN_IMAGE_EXPORT int32_t zan_image_load_svg(const char *key, const char *text, int32_t len,
                                 int32_t rasterW, int32_t rasterH);

/* 解码文件或查询已注册内存键图像，返回借用像素描述符（下次缓存变更前有效） */
extern ZAN_IMAGE_EXPORT const zan_bitmap *zan_image_get(const char *key);

/* 在 "mem:" 键注册或替换 ARGB32 像素数据，成功返回宽度，非法输入返回 0 */
extern ZAN_IMAGE_EXPORT int32_t zan_image_register_argb(const char *key, const uint32_t *pixels,
                                      int w, int h, int stride);

/* 把已解码图像的像素复制到调用方的缓冲区（0xAARRGGBB），返回复制的像素数 */
extern ZAN_IMAGE_EXPORT int32_t zan_image_copy_argb(const char *key, uint32_t *dst,
                                      int32_t max);

#ifdef __cplusplus
}
#endif

#endif
