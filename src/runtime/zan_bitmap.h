#ifndef ZAN_BITMAP_H
#define ZAN_BITMAP_H

#include <stdint.h>

/* 底层系统交互与数据协议契约 */
typedef struct zan_bitmap {
    const uint32_t *pixels;
    int width, height, stride;
    uint64_t serial;
} zan_bitmap;

#endif
