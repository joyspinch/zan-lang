#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int64_t i64;
typedef int32_t i32;
typedef uint32_t u32;

extern i64 zan_gui_create_surface(i64 width, i64 height);
extern i64 zan_gui_destroy_surface(i64 id);
extern void zan_gui_clear(i64 surface_id, i64 color);
extern void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size);
extern i64 zan_gui_measure_text(const char *text, i64 font_size);
extern void zan_gui_draw_line(i64 surface_id, i64 x0, i64 y0, i64 x1, i64 y1,
                              i64 color, i64 thickness);
extern void *zan_gui_get_pixels(i64 surface_id);
extern void zan_gui_fill_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                              i32 color);
extern void zan_gui_push_clip(i32 surface_id, i32 x, i32 y, i32 w, i32 h);
extern void zan_gui_pop_clip(i32 surface_id);
extern void zan_gui_blur_rect_cached(i32 surface_id, i32 x, i32 y, i32 w,
                                     i32 h, i32 radius, i32 slot, i32 dirty);
extern i32 zan_gui_stat_read(i32 idx, i32 kind);

static int changed(const u32 *pixels, int count, u32 background) {
    for (int i = 0; i < count; i++)
        if (pixels[i] != background) return 1;
    return 0;
}

/* assert() is compiled out in the Release build the CI configures, so the
 * blur-cache checks below state their failures themselves. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "gui runtime: %s failed (line %d)\n", #cond, __LINE__); \
        exit(1); \
    } \
} while (0)

/* stat idx 5 = blur (recomputed), 6 = blur cache hit (restored). Reading
 * kind 1 clears both fields, so each probe below starts from zero. */
static i32 blur_calls(void) { return zan_gui_stat_read(5, 0); }
static i32 blur_hits(void) { return zan_gui_stat_read(6, 0); }
static void blur_stats_reset(void) {
    zan_gui_stat_read(5, 1);
    zan_gui_stat_read(6, 1);
}

static void paint_backdrop(i32 sid, i32 tint) {
    zan_gui_clear(sid, 0xFF203040);
    zan_gui_fill_rect(sid, 8, 8, 96, 40, tint);
    zan_gui_fill_rect(sid, 24, 60, 72, 40, 0xFF806040);
}

/* The glass panel's blur is cached per slot, and a damage-clipped frame only
 * lets part of the surface be written; these cover the three region/clip
 * relations a partial frame produces. */
static void test_blur_cache_clip(void) {
    const i32 bx = 16, by = 16, bw = 64, bh = 64, br = 12, slot = 3;
    i64 surface = zan_gui_create_surface(128, 128);
    CHECK(surface >= 0);
    const i32 sid = (i32)surface;
    u32 *px = (u32 *)zan_gui_get_pixels(surface);
    CHECK(px);

    /* First paint of the panel: nothing cached yet, so the blur runs. */
    paint_backdrop(sid, 0xFF4080C0);
    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 1);
    CHECK(blur_calls() == 1);
    CHECK(blur_hits() == 0);

    /* Same geometry, backdrop untouched: restored from the slot. */
    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 0);
    CHECK(blur_calls() == 0);
    CHECK(blur_hits() == 1);

    /* Damage strip above the panel: not one pixel of the region is writable,
     * so neither the blur nor the cached restore may run at all. */
    u32 before[128 * 128];
    for (int i = 0; i < 128 * 128; i++) before[i] = px[i];
    zan_gui_push_clip(sid, 0, 0, 128, 8);
    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 1);
    CHECK(blur_calls() == 0);
    CHECK(blur_hits() == 0);
    for (int i = 0; i < 128 * 128; i++) CHECK(px[i] == before[i]);
    zan_gui_pop_clip(sid);

    /* Panel straddling the damage strip: the blur runs, but only the clipped
     * part of it reached the surface, so the slot must not keep that mixture
     * of blurred and sharp pixels -- the next frame has to blur again. */
    zan_gui_push_clip(sid, 0, 0, 128, 48);
    zan_gui_fill_rect(sid, bx, by, bw, 16, 0xFFC02040);
    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 1);
    CHECK(blur_calls() == 1);
    zan_gui_pop_clip(sid);

    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 0);
    CHECK(blur_calls() == 1);
    CHECK(blur_hits() == 0);

    /* And the refilled slot is usable again. */
    blur_stats_reset();
    zan_gui_blur_rect_cached(sid, bx, by, bw, bh, br, slot, 0);
    CHECK(blur_calls() == 0);
    CHECK(blur_hits() == 1);

    CHECK(zan_gui_destroy_surface(surface) == 0);
}

int main(void) {
    const u32 background = 0xFFFFFFFFu;
    i64 surface = zan_gui_create_surface(64, 64);
    assert(surface >= 0);
    u32 *pixels = (u32 *)zan_gui_get_pixels(surface);
    assert(pixels);

    zan_gui_clear(surface, background);
    zan_gui_draw_line(surface, 8, 8, 40, 40, 0xFF112233u, 2);
    assert(changed(pixels, 64 * 64, background));

    i64 one = zan_gui_measure_text("\xC3\xA9", 14);
    i64 two = zan_gui_measure_text("\xC3\xA9\xC3\xA9", 14);
    assert(one > 0);
    assert(two > one);

    zan_gui_clear(surface, background);
    zan_gui_draw_text(surface, 2, 2, "\xC3\xA9", 0xFF334455u, 14);
    assert(changed(pixels, 64 * 64, background));

    assert(zan_gui_destroy_surface(surface) == 0);

    test_blur_cache_clip();

    puts("gui runtime ok");
    return 0;
}
