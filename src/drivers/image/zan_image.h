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

/* 将 ARGB32 (0xAARRGGBB) 像素编码为目标格式：
 * format: 2=JPEG, 3=BMP(32-bit BGRA), 4=TGA(32-bit BGRA), 5=BMP(24-bit BGR)
 * quality: JPEG 质量 1..100（其他格式忽略）
 * 成功返回非 NULL 缓冲区指针并写入 *out_len，调用方用 zan_image_free_bytes 释放。 */
extern ZAN_IMAGE_EXPORT uint8_t *zan_image_encode_argb(const uint32_t *pixels,
                                      int32_t w, int32_t h, int32_t stride,
                                      int32_t format, int32_t quality,
                                      int32_t *out_len);
extern ZAN_IMAGE_EXPORT void zan_image_free_bytes(void *ptr);

/* 多帧动图解码接口（支持 GIF 全帧解码；静态图退化为 1 帧）。返回非 0 句柄，失败返回 0。 */
extern ZAN_IMAGE_EXPORT intptr_t zan_image_anim_open_mem(const uint8_t *data, int32_t len);
extern ZAN_IMAGE_EXPORT intptr_t zan_image_anim_open_file(const char *path);
extern ZAN_IMAGE_EXPORT int32_t zan_image_anim_width(intptr_t handle);
extern ZAN_IMAGE_EXPORT int32_t zan_image_anim_height(intptr_t handle);
extern ZAN_IMAGE_EXPORT int32_t zan_image_anim_frame_count(intptr_t handle);
extern ZAN_IMAGE_EXPORT int32_t zan_image_anim_frame_delay_ms(intptr_t handle, int32_t index);
extern ZAN_IMAGE_EXPORT int32_t zan_image_anim_copy_frame(intptr_t handle, int32_t index,
                                      uint32_t *dst, int32_t max_pixels);
extern ZAN_IMAGE_EXPORT void zan_image_anim_close(intptr_t handle);

#ifdef __cplusplus
}
#endif

#endif
