#include <stdint.h>

__attribute__((visibility("default")))
int64_t zan_gui_event_seq(void) {
    static int64_t s_seq = 1;
    return s_seq++;
}

__attribute__((visibility("default")))
int32_t zan_gui_event_flag(void) {
    return 0;
}

__attribute__((visibility("default")))
void zan_gui_present_full(void) {
    /* macOS draws full surface on every swap */
}

extern int32_t zan_gui_font_height(int32_t font_size);

__attribute__((visibility("default")))
int32_t zan_gui_font_ascent(int32_t font_size) {
    if (font_size <= 0) return 0;
    int32_t h = zan_gui_font_height(font_size);
    if (h <= 0) h = font_size;
    return (int32_t)((h * 4 + 2) / 5);
}

extern void zan_gui_draw_text(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size);

__attribute__((visibility("default")))
void zan_gui_draw_text_bold(int32_t surface_id, int32_t x, int32_t y, const char *text, int32_t color, int32_t font_size) {
    zan_gui_draw_text(surface_id, x, y, text, color, font_size);
}

extern void zan_gui_draw_polyline(int32_t surface_id, const int32_t *pts, int32_t n, int32_t color, int32_t thickness);

__attribute__((visibility("default")))
void zan_gui_draw_polybatch(int32_t surface_id, const int32_t *pts, const int32_t *counts, int32_t n_paths, int32_t color, int32_t thickness) {
    if (!pts || !counts || n_paths < 1) return;
    int32_t off = 0;
    for (int32_t p = 0; p < n_paths; p++) {
        int32_t n = counts[p];
        if (n >= 2) {
            zan_gui_draw_polyline(surface_id, pts + off, n, color, thickness);
        }
        off += n * 2;
    }
}

