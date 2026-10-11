#ifndef ZAN_BITMAP_H
#define ZAN_BITMAP_H

#include <stdint.h>

typedef struct zan_bitmap {
    const uint32_t *pixels;
    int width, height, stride;
    uint64_t serial;
} zan_bitmap;

#endif
