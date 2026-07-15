#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include <stdint.h>

#if defined(_WIN32)
#define ZAN_SDL_API __declspec(dllexport)
#else
#define ZAN_SDL_API __attribute__((visibility("default")))
#endif

typedef int64_t zan_i64;

static SDL_Event zan_last_event;

static zan_i64 zan_bool(bool value) {
    return value ? 1 : 0;
}

static void *zan_ptr(zan_i64 handle) {
    return (void *)(intptr_t)handle;
}

static zan_i64 zan_handle(const void *ptr) {
    return (zan_i64)(intptr_t)ptr;
}

ZAN_SDL_API zan_i64 zan_sdl_init(zan_i64 flags) {
    return zan_bool(SDL_Init((SDL_InitFlags)(uint32_t)flags));
}

ZAN_SDL_API void zan_sdl_quit(void) {
    SDL_Quit();
}

ZAN_SDL_API const char *zan_sdl_get_error(void) {
    const char *error = SDL_GetError();
    return error ? error : "";
}

ZAN_SDL_API zan_i64 zan_sdl_version(void) {
    return (zan_i64)SDL_GetVersion();
}

ZAN_SDL_API zan_i64 zan_sdl_ticks(void) {
    return (zan_i64)SDL_GetTicks();
}

ZAN_SDL_API void zan_sdl_delay(zan_i64 milliseconds) {
    SDL_Delay((uint32_t)milliseconds);
}

ZAN_SDL_API zan_i64 zan_sdl_create_window(
    const char *title, zan_i64 width, zan_i64 height, zan_i64 flags) {
    SDL_Window *window = SDL_CreateWindow(
        title ? title : "", (int)width, (int)height, (SDL_WindowFlags)(uint64_t)flags);
    return zan_handle(window);
}

ZAN_SDL_API void zan_sdl_destroy_window(zan_i64 window) {
    if (window) SDL_DestroyWindow((SDL_Window *)zan_ptr(window));
}

ZAN_SDL_API zan_i64 zan_sdl_window_id(zan_i64 window) {
    return window ? (zan_i64)SDL_GetWindowID((SDL_Window *)zan_ptr(window)) : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_window_width(zan_i64 window) {
    int width = 0;
    if (!window || !SDL_GetWindowSize((SDL_Window *)zan_ptr(window), &width, NULL)) return 0;
    return (zan_i64)width;
}

ZAN_SDL_API zan_i64 zan_sdl_window_height(zan_i64 window) {
    int height = 0;
    if (!window || !SDL_GetWindowSize((SDL_Window *)zan_ptr(window), NULL, &height)) return 0;
    return (zan_i64)height;
}

ZAN_SDL_API zan_i64 zan_sdl_set_window_title(zan_i64 window, const char *title) {
    if (!window) return 0;
    return zan_bool(SDL_SetWindowTitle((SDL_Window *)zan_ptr(window), title ? title : ""));
}

ZAN_SDL_API zan_i64 zan_sdl_set_window_fullscreen(zan_i64 window, zan_i64 enabled) {
    if (!window) return 0;
    return zan_bool(SDL_SetWindowFullscreen((SDL_Window *)zan_ptr(window), enabled != 0));
}

ZAN_SDL_API zan_i64 zan_sdl_show_window(zan_i64 window) {
    return window ? zan_bool(SDL_ShowWindow((SDL_Window *)zan_ptr(window))) : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_hide_window(zan_i64 window) {
    return window ? zan_bool(SDL_HideWindow((SDL_Window *)zan_ptr(window))) : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_create_renderer(zan_i64 window, const char *name) {
    if (!window) return 0;
    if (name && name[0] == '\0') name = NULL;
    return zan_handle(SDL_CreateRenderer((SDL_Window *)zan_ptr(window), name));
}

ZAN_SDL_API void zan_sdl_destroy_renderer(zan_i64 renderer) {
    if (renderer) SDL_DestroyRenderer((SDL_Renderer *)zan_ptr(renderer));
}

ZAN_SDL_API const char *zan_sdl_renderer_name(zan_i64 renderer) {
    const char *name = renderer
        ? SDL_GetRendererName((SDL_Renderer *)zan_ptr(renderer))
        : NULL;
    return name ? name : "";
}

ZAN_SDL_API zan_i64 zan_sdl_set_render_vsync(zan_i64 renderer, zan_i64 enabled) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderVSync((SDL_Renderer *)zan_ptr(renderer), enabled ? 1 : 0));
}

ZAN_SDL_API zan_i64 zan_sdl_set_logical_size(
    zan_i64 renderer, zan_i64 width, zan_i64 height, zan_i64 mode) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderLogicalPresentation(
        (SDL_Renderer *)zan_ptr(renderer),
        (int)width,
        (int)height,
        (SDL_RendererLogicalPresentation)mode));
}

ZAN_SDL_API zan_i64 zan_sdl_set_draw_color(
    zan_i64 renderer, zan_i64 red, zan_i64 green, zan_i64 blue, zan_i64 alpha) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderDrawColor(
        (SDL_Renderer *)zan_ptr(renderer),
        (uint8_t)red, (uint8_t)green, (uint8_t)blue, (uint8_t)alpha));
}

ZAN_SDL_API zan_i64 zan_sdl_clear(zan_i64 renderer) {
    return renderer ? zan_bool(SDL_RenderClear((SDL_Renderer *)zan_ptr(renderer))) : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_present(zan_i64 renderer) {
    return renderer ? zan_bool(SDL_RenderPresent((SDL_Renderer *)zan_ptr(renderer))) : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_draw_point(zan_i64 renderer, zan_i64 x, zan_i64 y) {
    return renderer
        ? zan_bool(SDL_RenderPoint((SDL_Renderer *)zan_ptr(renderer), (float)x, (float)y))
        : 0;
}

ZAN_SDL_API zan_i64 zan_sdl_draw_line(
    zan_i64 renderer, zan_i64 x1, zan_i64 y1, zan_i64 x2, zan_i64 y2) {
    return renderer
        ? zan_bool(SDL_RenderLine(
            (SDL_Renderer *)zan_ptr(renderer),
            (float)x1, (float)y1, (float)x2, (float)y2))
        : 0;
}

static SDL_FRect zan_rect(zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
    SDL_FRect rect;
    rect.x = (float)x;
    rect.y = (float)y;
    rect.w = (float)width;
    rect.h = (float)height;
    return rect;
}

ZAN_SDL_API zan_i64 zan_sdl_draw_rect(
    zan_i64 renderer, zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
    if (!renderer) return 0;
    SDL_FRect rect = zan_rect(x, y, width, height);
    return zan_bool(SDL_RenderRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_i64 zan_sdl_fill_rect(
    zan_i64 renderer, zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
    if (!renderer) return 0;
    SDL_FRect rect = zan_rect(x, y, width, height);
    return zan_bool(SDL_RenderFillRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_i64 zan_sdl_create_texture_rgba32(
    zan_i64 renderer, zan_i64 width, zan_i64 height, zan_i64 streaming) {
    if (!renderer) return 0;
    SDL_TextureAccess access = streaming
        ? SDL_TEXTUREACCESS_STREAMING
        : SDL_TEXTUREACCESS_STATIC;
    SDL_Texture *texture = SDL_CreateTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        SDL_PIXELFORMAT_RGBA32,
        access,
        (int)width,
        (int)height);
    return zan_handle(texture);
}

ZAN_SDL_API zan_i64 zan_sdl_load_bmp_texture(zan_i64 renderer, const char *path) {
    if (!renderer || !path) return 0;
    SDL_Surface *surface = SDL_LoadBMP(path);
    if (!surface) return 0;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(
        (SDL_Renderer *)zan_ptr(renderer), surface);
    SDL_DestroySurface(surface);
    return zan_handle(texture);
}

ZAN_SDL_API void zan_sdl_destroy_texture(zan_i64 texture) {
    if (texture) SDL_DestroyTexture((SDL_Texture *)zan_ptr(texture));
}

ZAN_SDL_API zan_i64 zan_sdl_update_texture(
    zan_i64 texture, const void *pixels, zan_i64 pitch) {
    if (!texture || !pixels) return 0;
    return zan_bool(SDL_UpdateTexture(
        (SDL_Texture *)zan_ptr(texture), NULL, pixels, (int)pitch));
}

ZAN_SDL_API zan_i64 zan_sdl_set_texture_nearest(zan_i64 texture, zan_i64 nearest) {
    if (!texture) return 0;
    SDL_ScaleMode mode = nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
    return zan_bool(SDL_SetTextureScaleMode((SDL_Texture *)zan_ptr(texture), mode));
}

ZAN_SDL_API zan_i64 zan_sdl_render_texture(
    zan_i64 renderer, zan_i64 texture,
    zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
    if (!renderer || !texture) return 0;
    SDL_FRect dst = zan_rect(x, y, width, height);
    return zan_bool(SDL_RenderTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        (SDL_Texture *)zan_ptr(texture),
        NULL,
        &dst));
}

ZAN_SDL_API zan_i64 zan_sdl_poll_event(void) {
    return zan_bool(SDL_PollEvent(&zan_last_event));
}

ZAN_SDL_API zan_i64 zan_sdl_event_type(void) {
    return (zan_i64)zan_last_event.type;
}

ZAN_SDL_API zan_i64 zan_sdl_event_timestamp(void) {
    return (zan_i64)zan_last_event.common.timestamp;
}

ZAN_SDL_API zan_i64 zan_sdl_event_window_id(void) {
    return (zan_i64)zan_last_event.window.windowID;
}

ZAN_SDL_API zan_i64 zan_sdl_event_data1(void) {
    return (zan_i64)zan_last_event.window.data1;
}

ZAN_SDL_API zan_i64 zan_sdl_event_data2(void) {
    return (zan_i64)zan_last_event.window.data2;
}

ZAN_SDL_API zan_i64 zan_sdl_event_scancode(void) {
    return (zan_i64)zan_last_event.key.scancode;
}

ZAN_SDL_API zan_i64 zan_sdl_event_keycode(void) {
    return (zan_i64)zan_last_event.key.key;
}

ZAN_SDL_API zan_i64 zan_sdl_event_keymod(void) {
    return (zan_i64)zan_last_event.key.mod;
}

ZAN_SDL_API zan_i64 zan_sdl_event_repeat(void) {
    return zan_bool(zan_last_event.key.repeat);
}

ZAN_SDL_API double zan_sdl_event_mouse_x(void) {
    if (zan_last_event.type == SDL_EVENT_MOUSE_WHEEL)
        return (double)zan_last_event.wheel.mouse_x;
    if (zan_last_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        zan_last_event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return (double)zan_last_event.button.x;
    return (double)zan_last_event.motion.x;
}

ZAN_SDL_API double zan_sdl_event_mouse_y(void) {
    if (zan_last_event.type == SDL_EVENT_MOUSE_WHEEL)
        return (double)zan_last_event.wheel.mouse_y;
    if (zan_last_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        zan_last_event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return (double)zan_last_event.button.y;
    return (double)zan_last_event.motion.y;
}

ZAN_SDL_API double zan_sdl_event_mouse_dx(void) {
    if (zan_last_event.type == SDL_EVENT_MOUSE_WHEEL)
        return (double)zan_last_event.wheel.x;
    return (double)zan_last_event.motion.xrel;
}

ZAN_SDL_API double zan_sdl_event_mouse_dy(void) {
    if (zan_last_event.type == SDL_EVENT_MOUSE_WHEEL)
        return (double)zan_last_event.wheel.y;
    return (double)zan_last_event.motion.yrel;
}

ZAN_SDL_API zan_i64 zan_sdl_event_mouse_button(void) {
    return (zan_i64)zan_last_event.button.button;
}

ZAN_SDL_API zan_i64 zan_sdl_event_mouse_clicks(void) {
    return (zan_i64)zan_last_event.button.clicks;
}

ZAN_SDL_API const char *zan_sdl_event_text(void) {
    if (zan_last_event.type != SDL_EVENT_TEXT_INPUT || !zan_last_event.text.text)
        return "";
    return zan_last_event.text.text;
}

ZAN_SDL_API zan_i64 zan_sdl_key_down(zan_i64 scancode) {
    int count = 0;
    const bool *state = SDL_GetKeyboardState(&count);
    if (!state || scancode < 0 || scancode >= count) return 0;
    return zan_bool(state[(int)scancode]);
}
