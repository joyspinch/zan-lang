/* zan_image_anim.c -- 多帧动图解码器（GIF 全帧 + 静态图单帧回退）
 * 被 zan_image.c 以 unity 方式包含。 */

typedef struct zan_img_anim {
    int32_t width;
    int32_t height;
    int32_t frame_count;
    uint32_t *pixels;  /* frame_count * width * height, 0xAARRGGBB */
    int32_t *delays;   /* frame_count, milliseconds */
} zan_img_anim_t;

static int zan_img_is_gif_header(const uint8_t *data, int32_t len) {
    if (!data || len < 6) return 0;
    return (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0);
}

ZAN_IMAGE_EXPORT intptr_t zan_image_anim_open_mem(const uint8_t *data, int32_t len) {
    if (!data || len <= 0) return 0;

    if (zan_img_is_gif_header(data, len)) {
        int *delays = NULL;
        int w = 0, h = 0, z = 0, comp = 0;
        stbi_uc *rgba = stbi_load_gif_from_memory(data, len, &delays, &w, &h, &z, &comp, 4);
        if (rgba && z > 0 && zan_img_dimensions_ok(w, h)) {
            uint64_t frame_px = (uint64_t)w * (uint64_t)h;
            uint64_t total_px = frame_px * (uint64_t)z;
            /* 防止恶意超大动图耗尽内存（上限 128M 像素 = 512MB） */
            if (total_px > 0 && total_px <= 134217728ULL) {
                zan_img_anim_t *a = (zan_img_anim_t *)calloc(1, sizeof(zan_img_anim_t));
                uint32_t *pix = (uint32_t *)malloc((size_t)total_px * sizeof(uint32_t));
                int32_t *dly = (int32_t *)malloc((size_t)z * sizeof(int32_t));
                if (a && pix && dly) {
                    for (uint64_t i = 0; i < total_px; i++) {
                        uint32_t r = rgba[i * 4 + 0];
                        uint32_t g = rgba[i * 4 + 1];
                        uint32_t b = rgba[i * 4 + 2];
                        uint32_t al = rgba[i * 4 + 3];
                        pix[i] = (al << 24) | (r << 16) | (g << 8) | b;
                    }
                    for (int f = 0; f < z; f++) {
                        int d = delays ? delays[f] : 100;
                        /* GIF 规范与浏览器习惯：delay <= 10ms 视为默认 100ms 防止 0ms 死循环 */
                        dly[f] = (d <= 0) ? 100 : d;
                    }
                    a->width = w;
                    a->height = h;
                    a->frame_count = z;
                    a->pixels = pix;
                    a->delays = dly;
                    stbi_image_free(rgba);
                    if (delays) STBI_FREE(delays);
                    return (intptr_t)a;
                }
                free(a); free(pix); free(dly);
            }
        }
        if (rgba) stbi_image_free(rgba);
        if (delays) STBI_FREE(delays);
    }

    /* 回退为单帧静态图（支持 PNG/JPEG/BMP/WebP/TGA/PSD/PNM/ICO） */
    int w = 0, h = 0;
    uint32_t *pix = zan_img_decode(data, len, &w, &h);
    if (!pix || w <= 0 || h <= 0) {
        free(pix);
        return 0;
    }
    zan_img_anim_t *a = (zan_img_anim_t *)calloc(1, sizeof(zan_img_anim_t));
    int32_t *dly = (int32_t *)malloc(sizeof(int32_t));
    if (!a || !dly) {
        free(a); free(dly); free(pix);
        return 0;
    }
    dly[0] = 0;
    a->width = w;
    a->height = h;
    a->frame_count = 1;
    a->pixels = pix;
    a->delays = dly;
    return (intptr_t)a;
}

ZAN_IMAGE_EXPORT intptr_t zan_image_anim_open_file(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *fp = NULL;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen > 0) {
        wchar_t *wp = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
        if (wp) {
            MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, wlen);
            fp = _wfopen(wp, L"rb");
            free(wp);
        }
    }
#else
    fp = fopen(path, "rb");
#endif
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    long sz = ftell(fp);
    if (sz <= 0 || sz > 128 * 1024 * 1024L) { fclose(fp); return 0; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 0; }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (got != (size_t)sz) { free(buf); return 0; }
    intptr_t h = zan_image_anim_open_mem(buf, (int32_t)got);
    free(buf);
    return h;
}

ZAN_IMAGE_EXPORT int32_t zan_image_anim_width(intptr_t handle) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    return a ? a->width : 0;
}

ZAN_IMAGE_EXPORT int32_t zan_image_anim_height(intptr_t handle) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    return a ? a->height : 0;
}

ZAN_IMAGE_EXPORT int32_t zan_image_anim_frame_count(intptr_t handle) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    return a ? a->frame_count : 0;
}

ZAN_IMAGE_EXPORT int32_t zan_image_anim_frame_delay_ms(intptr_t handle, int32_t index) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    if (!a || index < 0 || index >= a->frame_count) return 0;
    return a->delays[index];
}

ZAN_IMAGE_EXPORT int32_t zan_image_anim_copy_frame(intptr_t handle, int32_t index,
                                                   uint32_t *dst, int32_t max_pixels) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    if (!a || !dst || index < 0 || index >= a->frame_count) return 0;
    int64_t count = (int64_t)a->width * (int64_t)a->height;
    if (count <= 0 || count > (int64_t)max_pixels) return 0;
    memcpy(dst, a->pixels + (size_t)index * (size_t)count, (size_t)count * sizeof(uint32_t));
    return (int32_t)count;
}

ZAN_IMAGE_EXPORT void zan_image_anim_close(intptr_t handle) {
    zan_img_anim_t *a = (zan_img_anim_t *)handle;
    if (!a) return;
    free(a->pixels);
    free(a->delays);
    free(a);
}
