/* Included by gui_runtime.c. The shell owns these buffers; no surface state or
   platform API is involved in preparing UpdateLayeredWindow's ARGB pixels. */

static int zan_layered_span(const void *pixels, i32 width, i32 height,
                            i32 stride, i32 pixel_bytes, size_t *bytes) {
    if (width < 0 || height < 0 || stride < 0) return 0;
    *bytes = 0;
    if (width == 0 || height == 0) return 1;
    int64_t row = (int64_t)width * pixel_bytes;
    int64_t span = (int64_t)(height - 1) * stride + row;
    if (!pixels || row > stride || span > PTRDIFF_MAX) return 0;
    if (pixel_bytes == 4 && (((uintptr_t)pixels | (uintptr_t)stride) & 3))
        return 0;
    if ((uint64_t)span > UINTPTR_MAX - (uintptr_t)pixels) return 0;
    *bytes = (size_t)span;
    return 1;
}

static int zan_layered_overlap(const void *a, size_t an,
                               const void *b, size_t bn) {
    if (!an || !bn) return 0;
    return (uintptr_t)a < (uintptr_t)b + bn &&
           (uintptr_t)b < (uintptr_t)a + an;
}

static inline u32 zan_layered_premultiply(u32 p, u32 a) {
    if (a == 255) return p;
    if (a == 0) return 0;
    /* Two separate floors are intentional: coverage scales alpha first, then
       straight RGB is multiplied by that alpha (WindowShapeMask.LayeredArgb). */
    u32 r = ((p >> 16) & 255) * a / 255;
    u32 g = ((p >> 8) & 255) * a / 255;
    u32 b = (p & 255) * a / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

EXPORT i32 zan_gui_prepare_layered_argb(void *dst, i32 dst_width,
    i32 dst_height, i32 dst_stride, const void *src, i32 src_width,
    i32 src_height, i32 src_stride, i32 offset_x, i32 offset_y,
    const u8 *coverage, i32 coverage_stride, i32 coverage_length,
    i32 shadow_band) {
    size_t dst_bytes, src_bytes, mask_bytes = 0;
    if (dst_width <= 0 || dst_height <= 0 ||
        !zan_layered_span(dst, dst_width, dst_height, dst_stride, 4, &dst_bytes) ||
        !zan_layered_span(src, src_width, src_height, src_stride, 4, &src_bytes))
        return 0;
    if (coverage &&
        (!zan_layered_span(coverage, src_width, src_height, coverage_stride, 1,
                           &mask_bytes) || coverage_length < 0 ||
         mask_bytes > (size_t)coverage_length))
        return 0;
    if (zan_layered_overlap(dst, dst_bytes, src, src_bytes) ||
        zan_layered_overlap(dst, dst_bytes, coverage, mask_bytes))
        return 0;

    /* Compute the intersection in 64 bits: even INT_MIN/MAX offsets are valid
       empty/clipped placements, without signed overflow or pre-buffer pointers. */
    int64_t left = offset_x > 0 ? offset_x : 0;
    int64_t right = (int64_t)offset_x + src_width;
    if (left > dst_width) left = dst_width;
    if (right < left) right = left;
    if (right > dst_width) right = dst_width;
    for (i32 y = 0; y < dst_height; ++y) {
        u8 *row = (u8 *)dst + (size_t)y * dst_stride;
        int64_t sy = (int64_t)y - offset_y;
        if (sy < 0 || sy >= src_height || left == right) {
            memset(row, 0, (size_t)dst_width * 4);
            continue;
        }
        memset(row, 0, (size_t)left * 4);
        memset(row + (size_t)right * 4, 0, (size_t)(dst_width - right) * 4);
        size_t sx = (size_t)(left - offset_x);
        const u32 *s = (const u32 *)((const u8 *)src + (size_t)sy * src_stride) + sx;
        u32 *d = (u32 *)row + (size_t)left;
        size_t count = (size_t)(right - left);
        if (!coverage) {
            for (size_t x = 0; x < count; ++x)
                d[x] = zan_layered_premultiply(s[x], s[x] >> 24);
        } else {
            const u8 *mask = coverage + (size_t)sy * coverage_stride + sx;
            for (size_t x = 0; x < count; ++x) {
                u32 p = s[x], a = p >> 24, cov = mask[x];
                if (cov == 0) {
                    if (!shadow_band || a == 0 || a >= 240) a = 0;
                } else if (cov != 255) {
                    a = a * cov / 255;
                }
                d[x] = zan_layered_premultiply(p, a);
            }
        }
    }
    return 1;
}
