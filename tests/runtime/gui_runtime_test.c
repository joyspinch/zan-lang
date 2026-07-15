#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef int64_t i64;
typedef uint32_t u32;

extern i64 zan_gui_create_surface(i64 width, i64 height);
extern i64 zan_gui_destroy_surface(i64 id);
extern void zan_gui_clear(i64 surface_id, i64 color);
extern void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size);
extern i64 zan_gui_measure_text(const char *text, i64 font_size);
extern void zan_gui_draw_icon(i64 surface_id, i64 x, i64 y, i64 box,
                              i64 color, i64 codepoint);
extern void *zan_gui_get_pixels(i64 surface_id);

static int changed(const u32 *pixels, int count, u32 background) {
    for (int i = 0; i < count; i++)
        if (pixels[i] != background) return 1;
    return 0;
}

int main(void) {
    const u32 background = 0xFFFFFFFFu;
    i64 surface = zan_gui_create_surface(64, 64);
    assert(surface >= 0);
    u32 *pixels = (u32 *)zan_gui_get_pixels(surface);
    assert(pixels);

    zan_gui_clear(surface, background);
    zan_gui_draw_icon(surface, 8, 8, 32, 0xFF112233u, 0xE8BB);
    assert(changed(pixels, 64 * 64, background));

    i64 one = zan_gui_measure_text("\xC3\xA9", 14);
    i64 two = zan_gui_measure_text("\xC3\xA9\xC3\xA9", 14);
    assert(one > 0);
    assert(two > one);

    zan_gui_clear(surface, background);
    zan_gui_draw_text(surface, 2, 2, "\xC3\xA9", 0xFF334455u, 14);
    assert(changed(pixels, 64 * 64, background));

    assert(zan_gui_destroy_surface(surface) == 0);
    puts("gui runtime ok");
    return 0;
}
