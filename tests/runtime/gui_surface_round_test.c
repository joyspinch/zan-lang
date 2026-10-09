/* 底层系统交互与数据协议契约 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include "../../src/runtime/zan_gui_graphics.h"

extern int32_t zan_gui_create_surface(int32_t, int32_t);
extern int32_t zan_gui_destroy_surface(int32_t);
extern void zan_gui_clear(int32_t, int32_t);
extern void *zan_gui_get_pixels(int32_t);
extern void zan_gui_push_clip(int32_t, int32_t, int32_t, int32_t, int32_t);
extern void zan_gui_pop_clip(int32_t);
extern void zan_gui_fill_rounded_rect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
extern void zan_gui_surface_rounded_rect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
extern void zan_gui_surface_rounded_rect_mask(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "surface round line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

/* 底层系统交互与数据协议契约 */
static double coverage(int x, int y, int w, int h, int r) {
    if (x < 0 || y < 0 || x >= w || y >= h || w <= 0 || h <= 0) return 0;
    if (r < 0) r = 0;
    if (r > w/2) r = w/2;
    if (r > h/2) r = h/2;
    double dx = x < r ? r-x-0.5 : (x >= w-r ? x+0.5-(w-r) : 0);
    double dy = y < r ? r-y-0.5 : (y >= h-r ? y+0.5-(h-r) : 0);
    if (!dx || !dy) return 1;
    double a = r + 0.5 - sqrt(dx*dx+dy*dy);
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}
static uint32_t reference(uint32_t bg, uint32_t f, uint32_t b, double o, double in) {
    double af = (f >> 24)/255.0, ab = (b >> 24)/255.0, ad = (bg >> 24)/255.0;
    double ring = o-in, fw = af*(in+ring*(1-ab)), bw = ab*ring;
    double dw = ad*(1-fw-bw), a = fw+bw+dw;
    if (a <= 0) return bg;
    uint32_t out = (uint32_t)(a*255+0.5) << 24;
    for (int s = 0; s <= 16; s += 8) {
        double v = (((f >> s)&255)*fw + ((b >> s)&255)*bw + ((bg >> s)&255)*dw)/a;
        out |= (uint32_t)(v+0.5) << s;
    }
    return out;
}
static void near_pixel(uint32_t got, uint32_t want) {
    for (int s = 0; s <= 24; s += 8) {
        int d = (int)((got >> s)&255) - (int)((want >> s)&255);
        if (d < -4 || d > 4) {
            fprintf(stderr, "pixel got %08x expected %08x\n", got, want);
            CHECK(0);
        }
    }
}
static void bitmap_boundaries(void) {
    const int invalid[] = {0, -1, 16385, INT_MAX};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        CHECK(zan_gui_create_surface(invalid[i], 1) == -1);
        CHECK(zan_gui_create_surface(1, invalid[i]) == -1);
    }
    int32_t id = zan_gui_create_surface(3, 2);
    CHECK(id >= 0);
    uint32_t *dst = zan_gui_get_pixels(id);
    uint32_t *src = malloc(5 * sizeof *src);
    CHECK(dst && src);
    src[0] = 0xff112233u; src[1] = 0xff223344u;
    src[2] = 0xdeadbeefu;
    src[3] = 0xff334455u; src[4] = 0xff445566u;
    zan_bitmap img = {0};
    img.pixels = src; img.width = 2; img.height = 2; img.stride = 3;
    /* 底层系统交互与数据协议契约 */
    zan_gui_clear(id, 0);
    zan_gui_blit_pixels(id, &img, 2, 1, 1, 1, 1, 1, 1, 1);
    CHECK(dst[5] == src[4]);
    for (int i = 0; i < 5; i++) CHECK(dst[i] == 0);
    zan_gui_blit_pixels(id, &img, 2, 1, 2, 2, 0, 0, 2, 2);
    CHECK(dst[5] == src[0]);
    zan_gui_blit_pixels(id, &img, -1, -1, 2, 2, 0, 0, 2, 2);
    CHECK(dst[0] == src[4]);
    img.stride = 1;
    zan_gui_blit_pixels(id, &img, 0, 0, 2, 2, 0, 0, 2, 2);
    CHECK(dst[0] == src[4] && dst[1] == 0);
    img.stride = 3;
    zan_gui_blit_pixels(id, &img, INT_MAX, INT_MAX, 2, 2, 0, 0, 2, 2);
    zan_gui_blit_pixels(id, &img, INT_MIN, INT_MIN, 2, 2, 0, 0, 2, 2);
    CHECK(dst[0] == src[4] && dst[5] == src[0]);
    CHECK(src[2] == 0xdeadbeefu);
    free(src);
    CHECK(zan_gui_destroy_surface(id) == 0);
    CHECK(zan_gui_get_pixels(id) == NULL);
    int32_t next = zan_gui_create_surface(1, 1);
    CHECK(next == id);
    CHECK(zan_gui_destroy_surface(next) == 0);
}

int main(void) {
    bitmap_boundaries();
    int32_t id = zan_gui_create_surface(32, 32);
    CHECK(id >= 0);
    uint32_t *p = zan_gui_get_pixels(id);
    CHECK(p != NULL);
    const uint32_t backgrounds[] = {0, 0xff203040u, 0x80406080u};
    const uint32_t fills[] = {0xffcc3311u, 0x80cc3311u, 0};
    const uint32_t borders[] = {0xff2288eeu, 0x802288eeu, 0};
    const int radii[] = {0, 1, 7, 100};
    const int widths[] = {0, -1, 1, 3, 20, INT_MAX};
    for (int bg = 0; bg < 3; bg++) for (int f = 0; f < 3; f++)
    for (int b = 0; b < 3; b++) for (int ri = 0; ri < 4; ri++)
    for (int ti = 0; ti < 6; ti++) {
        int r = radii[ri] > 10 ? 10 : radii[ri], t = widths[ti];
        zan_gui_clear(id, (int32_t)backgrounds[bg]);
        zan_gui_surface_rounded_rect(id, 4, 4, 24, 20, radii[ri],
            (int32_t)fills[f], (int32_t)borders[b], t);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            double o = coverage(x-4,y-4,24,20,r), in = o;
            if (t > 0) in = t >= 10 ? 0 : coverage(x-4-t,y-4-t,24-2*t,20-2*t,r>t?r-t:0);
            if (in > o) in = o;
            near_pixel(p[y*32+x], reference(backgrounds[bg], fills[f], borders[b], o, in));
        }
    }
    /* 底层系统交互与数据协议契约 */
    uint32_t one[1024];
    zan_gui_clear(id, 0);
    zan_gui_fill_rounded_rect(id, 4, 4, 24, 20, 7, (int32_t)0xff2288eeu);
    for (int i=0;i<1024;i++) one[i]=p[i];
    zan_gui_clear(id, 0);
    zan_gui_surface_rounded_rect(id,4,4,24,20,7,(int32_t)0xffcc3311u,(int32_t)0xff2288eeu,3);
    int fractional = 0;
    for (int i=0;i<1024;i++) if ((one[i]>>24)>0 && (one[i]>>24)<255) {
        CHECK(p[i] == one[i]); fractional++;
    }
    CHECK(fractional > 0);
    /* 底层系统交互与数据协议契约 */
    zan_gui_clear(id, 0);
    zan_gui_push_clip(id,4,4,12,12);
    zan_gui_push_clip(id,4,4,6,6);
    zan_gui_surface_rounded_rect_mask(id,4,4,24,20,7,14,(int32_t)0xffcc3311u,(int32_t)0xff2288eeu,2);
    CHECK(p[4*32+4] == 0xff2288eeu);
    CHECK(p[6*32+6] == 0xffcc3311u);
    CHECK(p[4*32+10] == 0);
    zan_gui_pop_clip(id); zan_gui_pop_clip(id);
    CHECK(zan_gui_destroy_surface(id) == 0);
    puts("surface round coverage ok");
    return 0;
}
