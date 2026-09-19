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
