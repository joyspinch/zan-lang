/* SVG rasterization for zan_gui's in-memory image registry (see
 * gui_runtime.c, zan_gui_image_load_svg). Parses the document with the
 * vendored nanosvg (src/runtime/nanosvg, MIT) and rasterizes it into
 * ARGB32 pixels -- the same layout the stb_image / libwebp paths produce.
 * Compiled as its own TU: nanosvg's implementation headers are large and
 * only needed for SVG sources. */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
/* Upstream spells the raster gate without the "ER" (NANOSVGRAST_H). */
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

/* Rasterize `len` bytes of SVG source (NUL-terminated text is not required)
 * into outPix (malloc'd ARGB32, caller frees via zan_gui_image_evict).
 * boxW/boxH: fit the document inside, preserving aspect (contain); values
 * <= 0 mean "use the document's intrinsic size". Returns 1 and sets
 * outPix/outW/outH on success, 0 on parse or allocation failure. */
int zan_svg_raster(const char *text, int len, uint32_t **outPix, int *outW,
                   int *outH, int boxW, int boxH) {
    char *doc;
    NSVGimage *img;
    NSVGrasterizer *rast;
    unsigned char *rgba = NULL;
    uint32_t *pix = NULL;
    float iw, ih, scale;
    int w, h, x, n;

    *outPix = NULL; *outW = 0; *outH = 0;
    if (!text || len <= 0) return 0;
    doc = (char *)malloc((size_t)len + 1);
    if (!doc) return 0;
    memcpy(doc, text, (size_t)len);
    doc[len] = '\0';
    img = nsvgParse(doc, "px", 96);
    if (!img) { free(doc); return 0; }
    iw = img->width;
    ih = img->height;
    /* No width/height and no viewBox: nothing drawable, treat as a decode
     * failure rather than inventing an intrinsic size. */
    if (iw <= 0.0f || ih <= 0.0f) {
        nsvgDelete(img); free(doc); return 0;
    }
    if (boxW <= 0) boxW = (int)(iw + 0.5f);
    if (boxH <= 0) boxH = (int)(ih + 0.5f);
    if (boxW <= 0 || boxH <= 0) { nsvgDelete(img); free(doc); return 0; }
    /* Contain fit: uniform scale, document centered on (0,0) origin. Cap the
     * output -- a raster is w*h*4 bytes and the box comes from layout. */
    scale = boxW / iw < boxH / ih ? boxW / iw : boxH / ih;
    if (scale <= 0.0f || !(scale < 1e6f)) scale = 1.0f;
    w = (int)(iw * scale + 0.5f);
    h = (int)(ih * scale + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 8192) w = 8192;
    if (h > 8192) h = 8192;
    if ((long long)w * (long long)h > 16LL * 1024 * 1024) {
        nsvgDelete(img); free(doc); return 0;
    }
    rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); free(doc); return 0; }
    rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (rgba) {
        nsvgRasterize(rast, img, 0.0f, 0.0f, scale, rgba, w, h, w * 4);
        pix = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (pix) {
            /* nanosvg emits straight-alpha RGBA; the surface wants the
             * runtime's ARGB32 layout (same conversion as the stb path). */
            n = w * h;
            for (x = 0; x < n; x++) {
                unsigned char r = rgba[x*4], g = rgba[x*4+1],
                              b = rgba[x*4+2], a = rgba[x*4+3];
                pix[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16)
                       | ((uint32_t)g << 8) | (uint32_t)b;
            }
            *outPix = pix;
            *outW = w;
            *outH = h;
        }
        free(rgba);
    }
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    free(doc);
    return *outPix != NULL;
}
