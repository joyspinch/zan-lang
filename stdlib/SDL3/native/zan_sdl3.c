#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#if defined(_WIN32)
#define ZAN_SDL_API __declspec(dllexport)
#else
#define ZAN_SDL_API __attribute__((visibility("default")))
#endif

typedef int32_t zan_i32;
typedef intptr_t zan_iptr;
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

ZAN_SDL_API zan_i32 zan_sdl_init(zan_i32 flags) {
    return zan_bool(SDL_Init((SDL_InitFlags)(uint32_t)flags));
}

ZAN_SDL_API void zan_sdl_quit(void) {
    SDL_Quit();
}

ZAN_SDL_API const char *zan_sdl_get_error(void) {
    const char *error = SDL_GetError();
    return error ? error : "";
}

ZAN_SDL_API zan_i32 zan_sdl_version(void) {
    return (zan_i64)SDL_GetVersion();
}

ZAN_SDL_API zan_i64 zan_sdl_ticks(void) {
    return (zan_i64)SDL_GetTicks();
}

ZAN_SDL_API void zan_sdl_delay(zan_i32 milliseconds) {
    SDL_Delay((uint32_t)milliseconds);
}

ZAN_SDL_API zan_iptr zan_sdl_create_window(const char *title, zan_i32 width, zan_i32 height, zan_i32 flags) {
    SDL_Window *window = SDL_CreateWindow(
        title ? title : "", (int)width, (int)height, (SDL_WindowFlags)(uint64_t)flags);
    return zan_handle(window);
}

ZAN_SDL_API void zan_sdl_destroy_window(zan_iptr window) {
    if (window) SDL_DestroyWindow((SDL_Window *)zan_ptr(window));
}

ZAN_SDL_API zan_i32 zan_sdl_window_id(zan_iptr window) {
    return window ? (zan_i64)SDL_GetWindowID((SDL_Window *)zan_ptr(window)) : 0;
}

ZAN_SDL_API zan_i32 zan_sdl_window_width(zan_iptr window) {
    int width = 0;
    if (!window || !SDL_GetWindowSize((SDL_Window *)zan_ptr(window), &width, NULL)) return 0;
    return (zan_i64)width;
}

ZAN_SDL_API zan_i32 zan_sdl_window_height(zan_iptr window) {
    int height = 0;
    if (!window || !SDL_GetWindowSize((SDL_Window *)zan_ptr(window), NULL, &height)) return 0;
    return (zan_i64)height;
}

ZAN_SDL_API zan_i32 zan_sdl_set_window_title(zan_iptr window, const char *title) {
    if (!window) return 0;
    return zan_bool(SDL_SetWindowTitle((SDL_Window *)zan_ptr(window), title ? title : ""));
}

ZAN_SDL_API zan_i32 zan_sdl_set_window_fullscreen(zan_iptr window, zan_i32 enabled) {
    if (!window) return 0;
    return zan_bool(SDL_SetWindowFullscreen((SDL_Window *)zan_ptr(window), enabled != 0));
}

ZAN_SDL_API zan_i32 zan_sdl_show_window(zan_iptr window) {
    return window ? zan_bool(SDL_ShowWindow((SDL_Window *)zan_ptr(window))) : 0;
}

ZAN_SDL_API zan_i32 zan_sdl_hide_window(zan_iptr window) {
    return window ? zan_bool(SDL_HideWindow((SDL_Window *)zan_ptr(window))) : 0;
}

ZAN_SDL_API zan_iptr zan_sdl_create_renderer(zan_iptr window, const char *name) {
    if (!window) return 0;
    if (name && name[0] == '\0') name = NULL;
    return zan_handle(SDL_CreateRenderer((SDL_Window *)zan_ptr(window), name));
}

/* ---- Borderless drag/resize (custom title bar) ---------------------------
 * SDL calls the hit-test callback on every mouse-move probe. The caption strip
 * is the top 40 design units; resize handles live in the outer 6 px frame and
 * win over the caption at the corners. The game draws its caption buttons in
 * *design* units (Host chrome: three 46x32 buttons, 8px gaps, 10px right
 * inset), while the callback receives window *pixel* coordinates -- so the
 * strip is mapped through the logical width passed to zan_sdl_set_game_hittest
 * and excluded from dragging; otherwise a pressed button would start a window
 * drag that swallows the click. */

static SDL_HitTestResult zan_sdl_game_hittest(SDL_Window *win,
                                              const SDL_Point *area,
                                              void *data) {
    int logicalW = data ? *(int *)data : 0;
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    (void)H;
    int x = area->x, y = area->y;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) {
        return SDL_HITTEST_NORMAL;
    }
    int b = 6;
    int left = x < b, right = x >= W - b, top = y < b, bottom = y >= H - b;
    if (left && top) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (left && bottom) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (right && top) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (right && bottom) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (logicalW > 0 && W > 0 && y * logicalW < 40 * W) {
        /* the bar is authored in design units; compare in the same space */
        int xL = x * logicalW / W;
        int yL = y * logicalW / W;
        const int BW = 46, BH = 32, GAP = 8, PAD = 10;
        int stripL = logicalW - PAD - BW * 3 - GAP * 2;
        int stripR = logicalW - PAD;
        if (xL >= stripL && xL < stripR && yL >= 4 && yL < 36) {
            return SDL_HITTEST_NORMAL;
        }
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

ZAN_SDL_API zan_i32 zan_sdl_set_game_hittest(zan_iptr window, zan_i32 logical_w) {
    if (!window) return 0;
    static int storedLogicalW;   /* one game window per process */
    storedLogicalW = (int)logical_w;
    return zan_bool(SDL_SetWindowHitTest((SDL_Window *)zan_ptr(window),
                                         zan_sdl_game_hittest,
                                         &storedLogicalW));
}

ZAN_SDL_API zan_i32 zan_sdl_show_window_menu(zan_iptr window, zan_i32 x, zan_i32 y) {
    if (!window) return 0;
    return zan_bool(SDL_ShowWindowSystemMenu((SDL_Window *)zan_ptr(window),
                                             (int)x, (int)y));
}

ZAN_SDL_API zan_i32 zan_sdl_maximize_window(zan_iptr window) {
    if (!window) return 0;
    SDL_MaximizeWindow((SDL_Window *)zan_ptr(window));
    return 1;
}

ZAN_SDL_API zan_i32 zan_sdl_minimize_window(zan_iptr window) {
    if (!window) return 0;
    SDL_MinimizeWindow((SDL_Window *)zan_ptr(window));
    return 1;
}

ZAN_SDL_API void zan_sdl_destroy_renderer(zan_iptr renderer) {
    if (renderer) SDL_DestroyRenderer((SDL_Renderer *)zan_ptr(renderer));
}

ZAN_SDL_API const char *zan_sdl_renderer_name(zan_iptr renderer) {
    const char *name = renderer
        ? SDL_GetRendererName((SDL_Renderer *)zan_ptr(renderer))
        : NULL;
    return name ? name : "";
}

ZAN_SDL_API zan_i32 zan_sdl_set_render_vsync(zan_iptr renderer, zan_i32 enabled) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderVSync((SDL_Renderer *)zan_ptr(renderer), enabled ? 1 : 0));
}

ZAN_SDL_API zan_i32 zan_sdl_set_logical_size(zan_iptr renderer, zan_i32 width, zan_i32 height, zan_i32 mode) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderLogicalPresentation(
        (SDL_Renderer *)zan_ptr(renderer),
        (int)width,
        (int)height,
        (SDL_RendererLogicalPresentation)mode));
}

ZAN_SDL_API double zan_sdl_window_to_render_x(zan_iptr renderer, double window_x, double window_y) {
    if (!renderer) return window_x;
    float x = (float)window_x;
    float y = (float)window_y;
    if (!SDL_RenderCoordinatesFromWindow(
            (SDL_Renderer *)zan_ptr(renderer),
            (float)window_x,
            (float)window_y,
            &x,
            &y)) {
        return window_x;
    }
    return (double)x;
}

ZAN_SDL_API double zan_sdl_window_to_render_y(zan_iptr renderer, double window_x, double window_y) {
    if (!renderer) return window_y;
    float x = (float)window_x;
    float y = (float)window_y;
    if (!SDL_RenderCoordinatesFromWindow(
            (SDL_Renderer *)zan_ptr(renderer),
            (float)window_x,
            (float)window_y,
            &x,
            &y)) {
        return window_y;
    }
    return (double)y;
}

ZAN_SDL_API zan_i32 zan_sdl_set_draw_color(
    zan_iptr renderer, zan_i32 red, zan_i32 green, zan_i32 blue,
    zan_i32 alpha) {
    if (!renderer) return 0;
    if (red == -1)
        return zan_bool(SDL_SetRenderClipRect(
            (SDL_Renderer *)zan_ptr(renderer), NULL));
    if (red < -1) {
        uint32_t packed = (uint32_t)(-2 - red);
        SDL_Rect clip = {
            (int)((packed >> 16) & 0xFFFF),
            (int)(packed & 0xFFFF),
            (int)green,
            (int)blue
        };
        return zan_bool(SDL_SetRenderClipRect(
            (SDL_Renderer *)zan_ptr(renderer), &clip));
    }
    return zan_bool(SDL_SetRenderDrawColor(
        (SDL_Renderer *)zan_ptr(renderer),
        (uint8_t)red, (uint8_t)green, (uint8_t)blue, (uint8_t)alpha));
}

ZAN_SDL_API zan_i32 zan_sdl_clear(zan_iptr renderer) {
    return renderer ? zan_bool(SDL_RenderClear((SDL_Renderer *)zan_ptr(renderer))) : 0;
}

ZAN_SDL_API zan_i32 zan_sdl_present(zan_iptr renderer) {
    return renderer ? zan_bool(SDL_RenderPresent((SDL_Renderer *)zan_ptr(renderer))) : 0;
}

/* Read the renderer's pixels back 到 a caller-supplied RGBA 缓冲区. Writes
 * 宽度*高度*4 bytes. 返回 1 on 成功, 0 on failure. */
ZAN_SDL_API zan_i32 zan_sdl_read_pixels(zan_iptr renderer, zan_iptr out_rgba, zan_i32 width, zan_i32 height) {
    if (!renderer || !out_rgba || width <= 0 || height <= 0) return 0;
    SDL_Surface *surface = SDL_RenderReadPixels((SDL_Renderer *)zan_ptr(renderer), NULL);
    if (!surface) return 0;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface *conv = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!conv) return 0;
        surface = conv;
    }
    int cw = surface->w, ch = surface->h;
    int n = width * height;
    if (cw != width || ch != height) {
        SDL_DestroySurface(surface);
        return 0;
    }
    if (surface->pitch == width * 4) {
        memcpy((void *)(intptr_t)out_rgba, surface->pixels, (size_t)n * 4);
    } else {
        const unsigned char *src = (const unsigned char *)surface->pixels;
        unsigned char *dst = (unsigned char *)(intptr_t)out_rgba;
        for (int y = 0; y < height; y++) {
            memcpy(dst + (size_t)y * width * 4, src + (size_t)y * surface->pitch,
                   (size_t)width * 4);
        }
    }
    SDL_DestroySurface(surface);
    return 1;
}

/* Current 渲染 target / backbuffer 尺寸 in pixels, packed as (h<<16)|w.
 * 0 when the renderer is unusable. */
ZAN_SDL_API zan_i32 zan_sdl_render_output_size(zan_iptr renderer) {
    if (!renderer) return 0;
    int w = 0, h = 0;
    if (!SDL_GetRenderOutputSize((SDL_Renderer *)zan_ptr(renderer), &w, &h)) return 0;
    return ((h & 0xFFFF) << 16) | (w & 0xFFFF);
}

ZAN_SDL_API zan_i32 zan_sdl_draw_point(zan_iptr renderer, zan_i32 x, zan_i32 y) {
    return renderer
        ? zan_bool(SDL_RenderPoint((SDL_Renderer *)zan_ptr(renderer), (float)x, (float)y))
        : 0;
}

ZAN_SDL_API zan_i32 zan_sdl_draw_line(zan_iptr renderer, zan_i32 x1, zan_i32 y1, zan_i32 x2, zan_i32 y2) {
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

ZAN_SDL_API zan_i32 zan_sdl_draw_rect(
    zan_iptr renderer, zan_i32 x, zan_i32 y, zan_i32 width, zan_i32 height) {
    if (!renderer) return 0;
    SDL_FRect rect = zan_rect(x, y, width, height);
    return zan_bool(SDL_RenderRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_i32 zan_sdl_fill_rect(
    zan_iptr renderer, zan_i32 x, zan_i32 y, zan_i32 width, zan_i32 height) {
    if (!renderer) return 0;
    SDL_FRect rect = zan_rect(x, y, width, height);
    return zan_bool(SDL_RenderFillRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_iptr zan_sdl_create_texture_rgba32(
    zan_iptr renderer, zan_i32 width, zan_i32 height, zan_i32 streaming) {
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
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return zan_handle(texture);
}

ZAN_SDL_API zan_iptr zan_sdl_load_bmp_texture(zan_iptr renderer, const char *path) {
    if (!renderer || !path) return 0;
    SDL_Surface *surface = SDL_LoadBMP(path);
    if (!surface) return 0;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(
        (SDL_Renderer *)zan_ptr(renderer), surface);
    SDL_DestroySurface(surface);
    return zan_handle(texture);
}

ZAN_SDL_API zan_iptr zan_sdl_load_bmp_texture_colorkey(
    zan_iptr renderer, const char *path, zan_i32 red, zan_i32 green,
    zan_i32 blue) {
    if (!renderer || !path) return 0;
    SDL_Surface *surface = SDL_LoadBMP(path);
    if (!surface) return 0;
    Uint32 key = SDL_MapSurfaceRGB(
        surface, (Uint8)red, (Uint8)green, (Uint8)blue);
    if (!SDL_SetSurfaceColorKey(surface, true, key)) {
        SDL_DestroySurface(surface);
        return 0;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(
        (SDL_Renderer *)zan_ptr(renderer), surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_DestroySurface(surface);
    return zan_handle(texture);
}

ZAN_SDL_API zan_iptr zan_sdl_load_image_texture(zan_iptr renderer, const char *path) {
    if (!renderer || !path) return 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        SDL_SetError("Unable to decode image '%s': %s",
                     path, reason ? reason : "unknown decoder error");
        return 0;
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
    if (!surface) {
        stbi_image_free(pixels);
        return 0;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(
        (SDL_Renderer *)zan_ptr(renderer), surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);
    return zan_handle(texture);
}

ZAN_SDL_API void zan_sdl_destroy_texture(zan_iptr texture) {
    if (texture) SDL_DestroyTexture((SDL_Texture *)zan_ptr(texture));
}

ZAN_SDL_API zan_i32 zan_sdl_update_texture(zan_iptr texture, const void *pixels, zan_i32 pitch) {
    if (!texture || !pixels) return 0;
    return zan_bool(SDL_UpdateTexture(
        (SDL_Texture *)zan_ptr(texture), NULL, pixels, (int)pitch));
}

ZAN_SDL_API zan_i32 zan_sdl_set_texture_nearest(zan_iptr texture, zan_i32 nearest) {
    if (!texture) return 0;
    if (nearest >= 256)
        return zan_bool(SDL_SetTextureAlphaMod(
            (SDL_Texture *)zan_ptr(texture), (uint8_t)(nearest - 256)));
    SDL_ScaleMode mode = nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
    return zan_bool(SDL_SetTextureScaleMode((SDL_Texture *)zan_ptr(texture), mode));
}

ZAN_SDL_API zan_i32 zan_sdl_render_texture(
    zan_iptr renderer, zan_iptr texture, zan_i32 x, zan_i32 y,
    zan_i32 width, zan_i32 height) {
    if (!renderer || !texture) return 0;
    uint64_t packed = (uint64_t)width;
    int actual_width = (int)(uint32_t)packed;
    int angle = (int)(uint32_t)(packed >> 32);
    SDL_FRect dst = zan_rect(x, y, actual_width, height);
    if (angle != 0)
        return zan_bool(SDL_RenderTextureRotated(
            (SDL_Renderer *)zan_ptr(renderer),
            (SDL_Texture *)zan_ptr(texture),
            NULL,
            &dst,
            (double)angle,
            NULL,
            SDL_FLIP_NONE));
    return zan_bool(SDL_RenderTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        (SDL_Texture *)zan_ptr(texture),
        NULL,
        &dst));
}

/* ---- 2D 渲染 backend extensions (渲染 targets, blend, clip) ----
 * These thin wrappers expose the SDL_Renderer features a GPU-accelerated GUI
 * 画布 needs: offscreen 渲染 targets (for downsample blur), source-region
 * texture 绘制 (atlas/scaled blit), scissor clipping and blend modes. They
 * work on every SDL_Renderer backend (D3D11/D3D12/Metal/Vulkan/OpenGL) 带有 no
 * custom shaders, so the cross-platform GPU 路径 is active today. */

static SDL_BlendMode zan_blend_mode(zan_i64 mode) {
    if (mode == 0) return SDL_BLENDMODE_NONE;
    if (mode == 2) return SDL_BLENDMODE_ADD;
    if (mode == 3) return SDL_BLENDMODE_MOD;
    return SDL_BLENDMODE_BLEND;
}

ZAN_SDL_API zan_iptr zan_sdl_create_target_texture(zan_iptr renderer, zan_i32 width, zan_i32 height) {
    if (!renderer) return 0;
    SDL_Texture *texture = SDL_CreateTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_TARGET,
        (int)width, (int)height);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return zan_handle(texture);
}

ZAN_SDL_API zan_i32 zan_sdl_set_render_target(zan_iptr renderer, zan_iptr texture) {
    if (!renderer) return 0;
    SDL_Texture *tex = texture ? (SDL_Texture *)zan_ptr(texture) : NULL;
    return zan_bool(SDL_SetRenderTarget((SDL_Renderer *)zan_ptr(renderer), tex));
}

ZAN_SDL_API zan_i32 zan_sdl_render_texture_region(
    zan_iptr renderer, zan_iptr texture, zan_i32 sx, zan_i32 sy,
    zan_i32 sw, zan_i32 sh, zan_i32 dx, zan_i32 dy, zan_i32 dw, zan_i32 dh) {
    if (!renderer || !texture) return 0;
    SDL_FRect src = zan_rect(sx, sy, sw, sh);
    SDL_FRect dst = zan_rect(dx, dy, dw, dh);
    return zan_bool(SDL_RenderTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        (SDL_Texture *)zan_ptr(texture), &src, &dst));
}

ZAN_SDL_API zan_i32 zan_sdl_set_texture_alpha(zan_iptr texture, zan_i32 alpha) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureAlphaMod(
        (SDL_Texture *)zan_ptr(texture), (Uint8)alpha));
}

ZAN_SDL_API zan_i32 zan_sdl_set_texture_color(zan_iptr texture, zan_i32 red, zan_i32 green, zan_i32 blue) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureColorMod(
        (SDL_Texture *)zan_ptr(texture), (Uint8)red, (Uint8)green, (Uint8)blue));
}

ZAN_SDL_API zan_i32 zan_sdl_set_texture_blend(zan_iptr texture, zan_i32 mode) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureBlendMode(
        (SDL_Texture *)zan_ptr(texture), zan_blend_mode(mode)));
}

ZAN_SDL_API zan_i32 zan_sdl_set_draw_blend(zan_iptr renderer, zan_i32 mode) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderDrawBlendMode(
        (SDL_Renderer *)zan_ptr(renderer), zan_blend_mode(mode)));
}

ZAN_SDL_API zan_i32 zan_sdl_set_render_clip(
    zan_iptr renderer, zan_i32 x, zan_i32 y, zan_i32 width, zan_i32 height) {
    if (!renderer) return 0;
    SDL_Rect rect;
    rect.x = (int)x; rect.y = (int)y; rect.w = (int)width; rect.h = (int)height;
    return zan_bool(SDL_SetRenderClipRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_i32 zan_sdl_clear_render_clip(zan_iptr renderer) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderClipRect((SDL_Renderer *)zan_ptr(renderer), NULL));
}

ZAN_SDL_API zan_i32 zan_sdl_poll_event(void) {
    return zan_bool(SDL_PollEvent(&zan_last_event));
}

ZAN_SDL_API zan_i32 zan_sdl_event_type(void) {
    return (zan_i64)zan_last_event.type;
}

ZAN_SDL_API zan_i64 zan_sdl_event_timestamp(void) {
    return (zan_i64)zan_last_event.common.timestamp;
}

ZAN_SDL_API zan_i32 zan_sdl_event_window_id(void) {
    return (zan_i64)zan_last_event.window.windowID;
}

ZAN_SDL_API zan_i32 zan_sdl_event_data1(void) {
    return (zan_i64)zan_last_event.window.data1;
}

ZAN_SDL_API zan_i32 zan_sdl_event_data2(void) {
    return (zan_i64)zan_last_event.window.data2;
}

ZAN_SDL_API zan_i32 zan_sdl_event_scancode(void) {
    return (zan_i64)zan_last_event.key.scancode;
}

ZAN_SDL_API zan_i32 zan_sdl_event_keycode(void) {
    return (zan_i64)zan_last_event.key.key;
}

ZAN_SDL_API zan_i32 zan_sdl_event_keymod(void) {
    return (zan_i64)zan_last_event.key.mod;
}

ZAN_SDL_API zan_i32 zan_sdl_event_repeat(void) {
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

ZAN_SDL_API zan_i32 zan_sdl_event_mouse_button(void) {
    return (zan_i64)zan_last_event.button.button;
}

ZAN_SDL_API zan_i32 zan_sdl_event_mouse_clicks(void) {
    return (zan_i64)zan_last_event.button.clicks;
}

ZAN_SDL_API const char *zan_sdl_event_text(void) {
    if (zan_last_event.type != SDL_EVENT_TEXT_INPUT || !zan_last_event.text.text)
        return "";
    return zan_last_event.text.text;
}

ZAN_SDL_API zan_i32 zan_sdl_key_down(zan_i32 scancode) {
    int count = 0;
    const bool *state = SDL_GetKeyboardState(&count);
    if (!state || scancode < 0 || scancode >= count) return 0;
    return zan_bool(state[(int)scancode]);
}

/* ===================================================================
 * SDL_GPU sprite-batch renderer.
 *
 * Higher-level Game.Core/Game.Render code binds these 通过
 * [DllImport("zan_sdl3")]. The frame model is:
 *   ctx = zan_gpu_create(窗口)
 *   loop:
 *     zan_gpu_begin(ctx, clear rgba)
 *     zan_gpu_draw(ctx, tex, dst rect, uv rect, tint rgba)  // batched
 *     zan_gpu_end(ctx)                                       // upload+submit
 *
 * Draws are accumulated on the CPU and flushed in one 渲染 pass, grouped
 * 到 runs of 相同的 texture to minimize state changes. Vertex positions
 * are in framebuffer pixels; the shader maps them to NDC via the drawable
 * 尺寸 pushed as a vertex uniform.
 *
 * Shaders are provided as Metal Shading Language, so the GPU 路径 is active
 * on the Metal backend today. SPIR-V (Vulkan/Linux) and DXIL (D3D12/Windows)
 * variants 可以是 added to enable those backends; until then zan_gpu_create
 * 返回 0 on non-Metal drivers and callers fall back to the 2D renderer.
 * =================================================================== */

static const char *ZAN_GPU_VS_MSL =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };\n"
    "struct VOut { float4 pos [[position]]; float2 uv; float4 col; };\n"
    "struct U { float2 screen; };\n"
    "vertex VOut vmain(VIn in [[stage_in]], constant U& u [[buffer(0)]]) {\n"
    "  VOut o;\n"
    "  float2 n = float2(in.pos.x / u.screen.x * 2.0 - 1.0, 1.0 - in.pos.y / u.screen.y * 2.0);\n"
    "  o.pos = float4(n, 0.0, 1.0);\n"
    "  o.uv = in.uv; o.col = in.col;\n"
    "  return o;\n"
    "}\n";

static const char *ZAN_GPU_FS_MSL =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VOut { float4 pos [[position]]; float2 uv; float4 col; };\n"
    "fragment float4 fmain(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "  return tex.sample(smp, in.uv) * in.col;\n"
    "}\n";

typedef struct { float x, y, u, v, r, g, b, a; } ZanGpuVertex;
typedef struct { SDL_GPUTexture *tex; int first; int count; } ZanGpuDraw;
/* A loaded texture carries its pixel 尺寸 so 绘制 can specify source
 * rectangles in pixels (atlas-friendly) and the bridge derives UVs. */
typedef struct { SDL_GPUTexture *tex; int w, h; } ZanGpuTex;

typedef struct {
    SDL_GPUDevice *device;
    SDL_Window *window;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *vbuf;
    Uint32 vbuf_cap;                 /* capacity in vertices */
    SDL_GPUTransferBuffer *upbuf;    /* reused vertex-upload staging buffer */
    Uint32 upbuf_cap;                /* capacity in bytes */
    ZanGpuVertex *verts; int vcount, vcap;
    ZanGpuDraw *draws;   int dcount, dcap;
    SDL_GPUCommandBuffer *cmd;       /* per-frame */
    SDL_GPUTexture *swap;            /* per-frame swapchain image */
    Uint32 fb_w, fb_h;
    float cr, cg, cb, ca;
    /* Offscreen 模式: 渲染 到 a fixed target and 读取 pixels back on the
     * CPU. Enables headless GPU verification 带有 no 窗口/display server. */
    int offscreen;
    SDL_GPUTexture *offtex;
    SDL_GPUTransferBuffer *readbuf;
    unsigned char *cpu;
    Uint32 off_w, off_h;
} ZanGpuCtx;

static SDL_GPUShader *zan_gpu_shader(
    SDL_GPUDevice *dev, const char *code, const char *entry,
    SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms) {
    SDL_GPUShaderCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.code = (const Uint8 *)code; ci.code_size = strlen(code);
    ci.entrypoint = entry; ci.format = SDL_GPU_SHADERFORMAT_MSL;
    ci.stage = stage; ci.num_samplers = samplers; ci.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(dev, &ci);
}

/* Build the sprite pipeline 用于 given 颜色 target format (swapchain
 * format for windowed contexts, R8G8B8A8 for offscreen). */
static SDL_GPUGraphicsPipeline *zan_gpu_make_pipeline(
    SDL_GPUDevice *dev, SDL_GPUTextureFormat fmt) {
    SDL_GPUShader *vs = zan_gpu_shader(dev, ZAN_GPU_VS_MSL, "vmain",
        SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *fs = zan_gpu_shader(dev, ZAN_GPU_FS_MSL, "fmain",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(dev, vs);
        if (fs) SDL_ReleaseGPUShader(dev, fs);
        return NULL;
    }
    SDL_GPUVertexBufferDescription vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.slot = 0; vbd.pitch = sizeof(ZanGpuVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[3];
    memset(attrs, 0, sizeof(attrs));
    attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = 8;
    attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 16;

    SDL_GPUColorTargetDescription ctd;
    memset(&ctd, 0, sizeof(ctd));
    ctd.format = fmt;
    ctd.blend_state.enable_blend = true;
    ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pci;
    memset(&pci, 0, sizeof(pci));
    pci.vertex_shader = vs; pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 3;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline *pipe = SDL_CreateGPUGraphicsPipeline(dev, &pci);
    SDL_ReleaseGPUShader(dev, vs);
    SDL_ReleaseGPUShader(dev, fs);
    return pipe;
}

static SDL_GPUSampler *zan_gpu_make_sampler(SDL_GPUDevice *dev) {
    SDL_GPUSamplerCreateInfo sci;
    memset(&sci, 0, sizeof(sci));
    sci.min_filter = SDL_GPU_FILTER_NEAREST; sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(dev, &sci);
}

static void zan_gpu_alloc(ZanGpuCtx *ctx) {
    ctx->vcap = 4096; ctx->verts = (ZanGpuVertex *)SDL_malloc(sizeof(ZanGpuVertex) * ctx->vcap);
    ctx->dcap = 256;  ctx->draws = (ZanGpuDraw *)SDL_malloc(sizeof(ZanGpuDraw) * ctx->dcap);
    ctx->vbuf_cap = 4096;
    SDL_GPUBufferCreateInfo bci;
    memset(&bci, 0, sizeof(bci));
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX; bci.size = sizeof(ZanGpuVertex) * ctx->vbuf_cap;
    ctx->vbuf = SDL_CreateGPUBuffer(ctx->device, &bci);
}

ZAN_SDL_API zan_iptr zan_gpu_create(zan_iptr window) {
    SDL_Window *win = (SDL_Window *)zan_ptr(window);
    if (!win) return 0;
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
    if (!dev) return 0;
    if (!SDL_ClaimWindowForGPUDevice(dev, win)) { SDL_DestroyGPUDevice(dev); return 0; }
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);
    SDL_GPUGraphicsPipeline *pipe = zan_gpu_make_pipeline(dev, fmt);
    if (!pipe) { SDL_ReleaseWindowFromGPUDevice(dev, win); SDL_DestroyGPUDevice(dev); return 0; }
    ZanGpuCtx *ctx = (ZanGpuCtx *)SDL_calloc(1, sizeof(ZanGpuCtx));
    ctx->device = dev; ctx->window = win; ctx->pipeline = pipe;
    ctx->sampler = zan_gpu_make_sampler(dev);
    zan_gpu_alloc(ctx);
    return zan_handle(ctx);
}

/* Windowless GPU context that 渲染 到 a WxH target 读取 back on the CPU;
 * for headless verification and offscreen composition. */
ZAN_SDL_API void zan_gpu_destroy(zan_iptr handle); /* defined below; used by the cpu-allocation failure path */
ZAN_SDL_API zan_iptr zan_gpu_create_offscreen(zan_i32 width, zan_i32 height) {
    /* Same ceiling as the GUI runtime's surfaces: 16384^2 px * 4 bytes = 1 GB
     * and the off_w*off_h*4 size below overflows 32-bit past ~46341^2. */
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return 0;
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
    if (!dev) return 0;
    SDL_GPUGraphicsPipeline *pipe =
        zan_gpu_make_pipeline(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    if (!pipe) { SDL_DestroyGPUDevice(dev); return 0; }
    ZanGpuCtx *ctx = (ZanGpuCtx *)SDL_calloc(1, sizeof(ZanGpuCtx));
    ctx->device = dev; ctx->pipeline = pipe;
    ctx->sampler = zan_gpu_make_sampler(dev);
    zan_gpu_alloc(ctx);
    ctx->offscreen = 1; ctx->off_w = (Uint32)width; ctx->off_h = (Uint32)height;
    SDL_GPUTextureCreateInfo tci;
    memset(&tci, 0, sizeof(tci));
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tci.width = ctx->off_w; tci.height = ctx->off_h;
    tci.layer_count_or_depth = 1; tci.num_levels = 1;
    ctx->offtex = SDL_CreateGPUTexture(dev, &tci);
    SDL_GPUTransferBufferCreateInfo dn;
    memset(&dn, 0, sizeof(dn));
    dn.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    dn.size = (Uint64)width * (Uint64)height * 4;
    ctx->readbuf = SDL_CreateGPUTransferBuffer(dev, &dn);
    ctx->cpu = (unsigned char *)SDL_calloc(1, dn.size);
    if (!ctx->cpu) { zan_gpu_destroy(zan_handle(ctx)); return 0; }
    return zan_handle(ctx);
}

/* Read one pixel 从 the 最后一个 offscreen frame, packed as 0xRRGGBBAA. */
ZAN_SDL_API zan_i32 zan_gpu_read_pixel(zan_iptr handle, zan_i32 x, zan_i32 y) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->offscreen || !ctx->cpu) return 0;
    if (x < 0 || y < 0 || (Uint32)x >= ctx->off_w || (Uint32)y >= ctx->off_h) return 0;
    unsigned char *p = ctx->cpu + ((size_t)((Uint32)y * ctx->off_w + (Uint32)x)) * 4;
    return ((zan_i64)p[0] << 24) | ((zan_i64)p[1] << 16) |
           ((zan_i64)p[2] << 8) | (zan_i64)p[3];
}

ZAN_SDL_API void zan_gpu_destroy(zan_iptr handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx) return;
    if (ctx->vbuf) SDL_ReleaseGPUBuffer(ctx->device, ctx->vbuf);
    if (ctx->upbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->upbuf);
    if (ctx->sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->sampler);
    if (ctx->pipeline) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->pipeline);
    if (ctx->offtex) SDL_ReleaseGPUTexture(ctx->device, ctx->offtex);
    if (ctx->readbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->readbuf);
    if (ctx->window) SDL_ReleaseWindowFromGPUDevice(ctx->device, ctx->window);
    if (ctx->device) SDL_DestroyGPUDevice(ctx->device);
    SDL_free(ctx->cpu); SDL_free(ctx->verts); SDL_free(ctx->draws); SDL_free(ctx);
}

/* Create a sampler texture 从 tightly packed RGBA32 pixels. */
static SDL_GPUTexture *zan_gpu_make_texture(
    ZanGpuCtx *ctx, int width, int height, const void *pixels) {
    if (!ctx || !pixels || width <= 0 || height <= 0) return NULL;
    SDL_GPUTextureCreateInfo tci;
    memset(&tci, 0, sizeof(tci));
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = (Uint32)width; tci.height = (Uint32)height;
    tci.layer_count_or_depth = 1; tci.num_levels = 1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(ctx->device, &tci);
    if (!tex) return NULL;

    Uint64 bytes64 = (Uint64)width * (Uint64)height * 4;
    if (bytes64 > (Uint64)0x7FFFFFFF) {
        /* 2 GB upload is beyond the Uint32 size field below; reject. */
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return NULL;
    }
    Uint32 bytes = (Uint32)bytes64;
    SDL_GPUTransferBufferCreateInfo up;
    memset(&up, 0, sizeof(up));
    up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; up.size = bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(ctx->device, &up);
    if (!tb) { SDL_ReleaseGPUTexture(ctx->device, tex); return NULL; }
    void *map = SDL_MapGPUTransferBuffer(ctx->device, tb, false);
    if (!map) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return NULL;
    }
    memcpy(map, pixels, bytes);
    SDL_UnmapGPUTransferBuffer(ctx->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo tti;
    memset(&tti, 0, sizeof(tti));
    tti.transfer_buffer = tb; tti.offset = 0;
    tti.pixels_per_row = (Uint32)width; tti.rows_per_layer = (Uint32)height;
    SDL_GPUTextureRegion reg;
    memset(&reg, 0, sizeof(reg));
    reg.texture = tex; reg.w = (Uint32)width; reg.h = (Uint32)height; reg.d = 1;
    SDL_UploadToGPUTexture(cp, &tti, &reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(ctx->device, true, &fence, 1);
    SDL_ReleaseGPUFence(ctx->device, fence);
    SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
    return tex;
}

static zan_i64 zan_gpu_wrap(SDL_GPUTexture *tex, int w, int h) {
    if (!tex) return 0;
    ZanGpuTex *t = (ZanGpuTex *)SDL_malloc(sizeof(ZanGpuTex));
    t->tex = tex; t->w = w; t->h = h;
    return zan_handle(t);
}

ZAN_SDL_API zan_iptr zan_gpu_load_texture(zan_iptr handle, zan_i32 width, zan_i32 height, const void *pixels) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    return zan_gpu_wrap(zan_gpu_make_texture(ctx, (int)width, (int)height, pixels),
                        (int)width, (int)height);
}

/* Solid white WxH texture; tint it via zan_gpu_draw 颜色 to 获取 any 颜色. */
ZAN_SDL_API zan_iptr zan_gpu_solid_texture(zan_iptr handle, zan_i32 width, zan_i32 height) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || width <= 0 || height <= 0 || width > 16384 || height > 16384) return 0;
    size_t bytes = (size_t)((Uint64)width * (Uint64)height * 4);
    unsigned char *px = (unsigned char *)SDL_malloc(bytes);
    if (!px) return 0;
    memset(px, 0xFF, bytes);
    SDL_GPUTexture *tex = zan_gpu_make_texture(ctx, (int)width, (int)height, px);
    SDL_free(px);
    return zan_gpu_wrap(tex, (int)width, (int)height);
}

/* Load a BMP 文件 as an RGBA sampler texture. */
ZAN_SDL_API zan_iptr zan_gpu_load_bmp(zan_iptr handle, const char *path) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !path) return 0;
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) return 0;
    SDL_Surface *rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgba) return 0;
    SDL_GPUTexture *tex = NULL;
    int tw = rgba->w, th = rgba->h;
    if (rgba->pitch == rgba->w * 4) {
        tex = zan_gpu_make_texture(ctx, rgba->w, rgba->h, rgba->pixels);
    } else {
        unsigned char *packed = (unsigned char *)SDL_malloc((size_t)rgba->w * rgba->h * 4);
        if (!packed) { SDL_DestroySurface(rgba); return 0; }
        for (int y = 0; y < rgba->h; y++)
            memcpy(packed + (size_t)y * rgba->w * 4,
                   (unsigned char *)rgba->pixels + (size_t)y * rgba->pitch,
                   (size_t)rgba->w * 4);
        tex = zan_gpu_make_texture(ctx, rgba->w, rgba->h, packed);
        SDL_free(packed);
    }
    SDL_DestroySurface(rgba);
    return zan_gpu_wrap(tex, tw, th);
}

/* Load PNG, JPEG and other stb_image formats as an RGBA sampler texture. */
ZAN_SDL_API zan_iptr zan_gpu_load_image(zan_iptr handle, const char *path) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !path) return 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        SDL_SetError("Unable to decode image '%s': %s",
                     path, reason ? reason : "unknown decoder error");
        return 0;
    }
    SDL_GPUTexture *texture = zan_gpu_make_texture(ctx, width, height, pixels);
    stbi_image_free(pixels);
    return zan_gpu_wrap(texture, width, height);
}

ZAN_SDL_API void zan_gpu_free_texture(zan_iptr handle, zan_iptr texture) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    if (ctx && t) {
        if (t->tex) SDL_ReleaseGPUTexture(ctx->device, t->tex);
        SDL_free(t);
    }
}

ZAN_SDL_API zan_i32 zan_gpu_texture_width(zan_iptr texture) {
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    return t ? t->w : 0;
}

ZAN_SDL_API zan_i32 zan_gpu_texture_height(zan_iptr texture) {
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    return t ? t->h : 0;
}

ZAN_SDL_API zan_i32 zan_gpu_begin(zan_iptr handle, zan_i32 r, zan_i32 g, zan_i32 b, zan_i32 a) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx) return 0;
    ctx->cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!ctx->cmd) return 0;
    if (ctx->offscreen) {
        ctx->swap = ctx->offtex; ctx->fb_w = ctx->off_w; ctx->fb_h = ctx->off_h;
    } else {
        ctx->swap = NULL; ctx->fb_w = 0; ctx->fb_h = 0;
        SDL_WaitAndAcquireGPUSwapchainTexture(ctx->cmd, ctx->window,
            &ctx->swap, &ctx->fb_w, &ctx->fb_h);
    }
    ctx->vcount = 0; ctx->dcount = 0;
    ctx->cr = (float)r / 255.0f; ctx->cg = (float)g / 255.0f;
    ctx->cb = (float)b / 255.0f; ctx->ca = (float)a / 255.0f;
    return 1;
}

static void zan_gpu_push_vertex(
    ZanGpuCtx *ctx, float x, float y, float u, float v,
    float r, float g, float b, float a) {
    ZanGpuVertex *vv = &ctx->verts[ctx->vcount++];
    vv->x = x; vv->y = y; vv->u = u; vv->v = v;
    vv->r = r; vv->g = g; vv->b = b; vv->a = a;
}

/* Draw texture region (sx,sy,sw,sh in source pixels; sw<=0 means whole
 * texture) 到 the destination rect (dx,dy,dw,dh in framebuffer pixels),
 * multiplied by the r,g,b,a tint (0-255). */
ZAN_SDL_API void zan_gpu_draw(
    zan_iptr handle, zan_iptr texture, zan_i32 dx, zan_i32 dy, zan_i32 dw,
    zan_i32 dh, zan_i32 sx, zan_i32 sy, zan_i32 sw, zan_i32 sh, zan_i32 r,
    zan_i32 g, zan_i32 b, zan_i32 a) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx) return;
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    if (!t || !t->tex) return;
    SDL_GPUTexture *tex = t->tex;
    if (ctx->vcount + 6 > ctx->vcap) {
        ctx->vcap *= 2;
        ctx->verts = (ZanGpuVertex *)SDL_realloc(ctx->verts, sizeof(ZanGpuVertex) * ctx->vcap);
    }
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (sw > 0 && sh > 0 && t->w > 0 && t->h > 0) {
        u0 = (float)sx / (float)t->w; v0 = (float)sy / (float)t->h;
        u1 = (float)(sx + sw) / (float)t->w; v1 = (float)(sy + sh) / (float)t->h;
    }
    float fr = (float)r / 255.0f, fg = (float)g / 255.0f;
    float fb = (float)b / 255.0f, fa = (float)a / 255.0f;
    float x0 = (float)dx, y0 = (float)dy, x1 = (float)(dx + dw), y1 = (float)(dy + dh);
    int base = ctx->vcount;
    zan_gpu_push_vertex(ctx, x0, y0, u0, v0, fr, fg, fb, fa);
    zan_gpu_push_vertex(ctx, x1, y0, u1, v0, fr, fg, fb, fa);
    zan_gpu_push_vertex(ctx, x0, y1, u0, v1, fr, fg, fb, fa);
    zan_gpu_push_vertex(ctx, x0, y1, u0, v1, fr, fg, fb, fa);
    zan_gpu_push_vertex(ctx, x1, y0, u1, v0, fr, fg, fb, fa);
    zan_gpu_push_vertex(ctx, x1, y1, u1, v1, fr, fg, fb, fa);
    if (ctx->dcount > 0 && ctx->draws[ctx->dcount - 1].tex == tex) {
        ctx->draws[ctx->dcount - 1].count += 6;
    } else {
        if (ctx->dcount + 1 > ctx->dcap) {
            ctx->dcap *= 2;
            ctx->draws = (ZanGpuDraw *)SDL_realloc(ctx->draws, sizeof(ZanGpuDraw) * ctx->dcap);
        }
        ctx->draws[ctx->dcount].tex = tex;
        ctx->draws[ctx->dcount].first = base;
        ctx->draws[ctx->dcount].count = 6;
        ctx->dcount++;
    }
}

ZAN_SDL_API void zan_gpu_end(zan_iptr handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->cmd) return;
    if (!ctx->swap) { SDL_SubmitGPUCommandBuffer(ctx->cmd); ctx->cmd = NULL; return; }

    if (ctx->vcount > 0) {
        if ((Uint32)ctx->vcount > ctx->vbuf_cap) {
            SDL_ReleaseGPUBuffer(ctx->device, ctx->vbuf);
            while ((Uint32)ctx->vcount > ctx->vbuf_cap) ctx->vbuf_cap *= 2;
            SDL_GPUBufferCreateInfo bci;
            memset(&bci, 0, sizeof(bci));
            bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bci.size = sizeof(ZanGpuVertex) * ctx->vbuf_cap;
            ctx->vbuf = SDL_CreateGPUBuffer(ctx->device, &bci);
        }
        Uint32 bytes = (Uint32)(ctx->vcount * (int)sizeof(ZanGpuVertex));
        /* Reuse the staging 缓冲区 跨越 frames; 仅 grow it when a frame
         * needs more room. Creating/releasing one per frame churned GPU driver
         * allocations for every game. */
        if (bytes > ctx->upbuf_cap) {
            if (ctx->upbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->upbuf);
            if (ctx->upbuf_cap == 0) { ctx->upbuf_cap = bytes; }
            while (bytes > ctx->upbuf_cap) ctx->upbuf_cap *= 2;
            SDL_GPUTransferBufferCreateInfo up;
            memset(&up, 0, sizeof(up));
            up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; up.size = ctx->upbuf_cap;
            ctx->upbuf = SDL_CreateGPUTransferBuffer(ctx->device, &up);
        }
        void *map = SDL_MapGPUTransferBuffer(ctx->device, ctx->upbuf, true);
        memcpy(map, ctx->verts, bytes);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upbuf);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(ctx->cmd);
        SDL_GPUTransferBufferLocation loc;
        memset(&loc, 0, sizeof(loc));
        loc.transfer_buffer = ctx->upbuf; loc.offset = 0;
        SDL_GPUBufferRegion reg;
        memset(&reg, 0, sizeof(reg));
        reg.buffer = ctx->vbuf; reg.offset = 0; reg.size = bytes;
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
    }

    SDL_GPUColorTargetInfo cti;
    memset(&cti, 0, sizeof(cti));
    cti.texture = ctx->swap;
    cti.clear_color.r = ctx->cr; cti.clear_color.g = ctx->cg;
    cti.clear_color.b = ctx->cb; cti.clear_color.a = ctx->ca;
    cti.load_op = SDL_GPU_LOADOP_CLEAR; cti.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(ctx->cmd, &cti, 1, NULL);
    if (ctx->vcount > 0) {
        SDL_BindGPUGraphicsPipeline(rp, ctx->pipeline);
        SDL_GPUBufferBinding vbb; vbb.buffer = ctx->vbuf; vbb.offset = 0;
        SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);
        float screen[2] = {(float)ctx->fb_w, (float)ctx->fb_h};
        SDL_PushGPUVertexUniformData(ctx->cmd, 0, screen, sizeof(screen));
        for (int i = 0; i < ctx->dcount; i++) {
            SDL_GPUTextureSamplerBinding tsb;
            tsb.texture = ctx->draws[i].tex; tsb.sampler = ctx->sampler;
            SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
            SDL_DrawGPUPrimitives(rp, ctx->draws[i].count, 1, ctx->draws[i].first, 0);
        }
    }
    SDL_EndGPURenderPass(rp);

    if (ctx->offscreen) {
        SDL_GPUCopyPass *cp2 = SDL_BeginGPUCopyPass(ctx->cmd);
        SDL_GPUTextureRegion dreg;
        memset(&dreg, 0, sizeof(dreg));
        dreg.texture = ctx->offtex; dreg.w = ctx->off_w; dreg.h = ctx->off_h; dreg.d = 1;
        SDL_GPUTextureTransferInfo dti;
        memset(&dti, 0, sizeof(dti));
        dti.transfer_buffer = ctx->readbuf; dti.offset = 0;
        dti.pixels_per_row = ctx->off_w; dti.rows_per_layer = ctx->off_h;
        SDL_DownloadFromGPUTexture(cp2, &dreg, &dti);
        SDL_EndGPUCopyPass(cp2);
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(ctx->cmd);
        SDL_WaitForGPUFences(ctx->device, true, &fence, 1);
        SDL_ReleaseGPUFence(ctx->device, fence);
        void *m = SDL_MapGPUTransferBuffer(ctx->device, ctx->readbuf, false);
        if (m) memcpy(ctx->cpu, m, ctx->off_w * ctx->off_h * 4);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->readbuf);
    } else {
        SDL_SubmitGPUCommandBuffer(ctx->cmd);
    }
    ctx->cmd = NULL; ctx->swap = NULL;
}

/* ===================================================================
 * Audio: WAV clips and mixed playback voices.
 *
 * One physical playback device is opened by zan_audio_open; every voice is an
 * SDL_AudioStream bound to it, so SDL does the mixing and a clip can be
 * playing several times at once. A looping voice re-queues its clip from the
 * stream's get-callback, which is how SDL3 asks for more data.
 *
 * A finished voice frees its stream but keeps its pool slot, and a handle
 * carries the slot's generation: a Zan `SdlVoice` that outlives its sound
 * therefore answers "not playing" instead of dereferencing a recycled slot.
 * =================================================================== */

#define ZAN_VOICE_SLOTS 64

typedef struct {
    Uint8 *pcm;
    Uint32 len;
    SDL_AudioSpec spec;
} ZanAudioClip;

typedef struct {
    SDL_AudioStream *stream;
    ZanAudioClip *clip;
    zan_i32 loop;
    zan_i32 gen;
} ZanVoice;

static zan_i32 zan_audio_ready;
static ZanVoice zan_voices[ZAN_VOICE_SLOTS];
static zan_i32 zan_voice_gen = 1;
static float zan_audio_gain = 1.0f;

static zan_i64 zan_voice_pack(zan_i32 slot, zan_i32 gen) {
    return ((zan_i64)gen << 8) | (zan_i64)(slot + 1);
}

/* The live voice a handle names, or NULL once its sound ended (or the slot was
 * handed to a newer voice). */
static ZanVoice *zan_voice_of(zan_i64 handle) {
    zan_i32 slot = (zan_i32)((handle & 255) - 1);
    zan_i32 gen = (zan_i32)(handle >> 8);
    if (slot < 0 || slot >= ZAN_VOICE_SLOTS) return NULL;
    ZanVoice *v = &zan_voices[slot];
    if (!v->stream || v->gen != gen) return NULL;
    return v;
}

static void zan_voice_release(ZanVoice *v) {
    if (!v->stream) return;
    /* Destroying the stream also closes the logical device it opened. */
    SDL_DestroyAudioStream(v->stream);
    v->stream = NULL;
    v->clip = NULL;
    v->loop = 0;
}

/* Samples of a bound stream that have not reached the device yet. A playback
 * stream converts on demand, so the queued (input) side is what says whether a
 * sound is still to be heard -- the converted side reads 0 for most of it. */
static int zan_voice_pending(const ZanVoice *v) {
    int queued = (int)SDL_GetAudioStreamQueued(v->stream);
    if (queued > 0) return queued;
    return (int)SDL_GetAudioStreamAvailable(v->stream);
}

/* Frees the streams of one-shot voices that have played out, so the pool is
 * never exhausted by sounds nobody stopped explicitly. */
static void zan_voice_reap(void) {
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        if (!v->stream || v->loop) continue;
        if (zan_voice_pending(v) <= 0) zan_voice_release(v);
    }
}

static void SDLCALL zan_voice_feed(
    void *userdata, SDL_AudioStream *stream, int additional, int total) {
    ZanVoice *v = (ZanVoice *)userdata;
    (void)total;
    if (!v || !v->loop || !v->clip || additional <= 0) return;
    SDL_PutAudioStreamData(stream, v->clip->pcm, (int)v->clip->len);
}

ZAN_SDL_API zan_i32 zan_audio_open(void) {
    if (zan_audio_ready) return 1;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    zan_audio_ready = 1;
    return 1;
}

ZAN_SDL_API void zan_audio_close(void) {
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_release(&zan_voices[i]);
    zan_audio_ready = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

ZAN_SDL_API zan_i32 zan_audio_is_open(void) {
    return zan_bool(zan_audio_ready != 0);
}

/* Master gain: applied to voices started from now on and to the ones still
 * playing, so a fade or a mute takes effect immediately. */
ZAN_SDL_API void zan_audio_set_volume(double volume) {
    if (volume < 0.0) volume = 0.0;
    zan_audio_gain = (float)volume;
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        if (v->stream) SDL_SetAudioStreamGain(v->stream, zan_audio_gain);
    }
}

ZAN_SDL_API double zan_audio_volume(void) {
    return (double)zan_audio_gain;
}

ZAN_SDL_API const char *zan_audio_driver_name(void) {
    const char *name = SDL_GetCurrentAudioDriver();
    return name ? name : "";
}

ZAN_SDL_API zan_i32 zan_audio_active_voices(void) {
    zan_voice_reap();
    zan_i32 n = 0;
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) {
        if (zan_voices[i].stream) n++;
    }
    return n;
}

ZAN_SDL_API zan_iptr zan_audio_load_wav(const char *path) {
    if (!path || !path[0]) return 0;
    ZanAudioClip *clip = (ZanAudioClip *)SDL_calloc(1, sizeof(ZanAudioClip));
    if (!clip) return 0;
    if (!SDL_LoadWAV(path, &clip->spec, &clip->pcm, &clip->len)) {
        SDL_free(clip);
        return 0;
    }
    return zan_handle(clip);
}

/* Same as zan_audio_load_wav, but parses the WAV from caller-owned memory
 * (e.g. a decrypted resource-pack entry). The buffer is only read inside
 * this call -- SDL_LoadWAV_IO copies the PCM out, so ownership never moves. */
ZAN_SDL_API zan_iptr zan_audio_load_wav_mem(const void *data, zan_i32 len) {
    if (!data || len <= 0) return 0;
    ZanAudioClip *clip = (ZanAudioClip *)SDL_calloc(1, sizeof(ZanAudioClip));
    if (!clip) return 0;
    SDL_IOStream *io = SDL_IOFromConstMem(data, (size_t)len);
    if (!io) {
        SDL_free(clip);
        return 0;
    }
    if (!SDL_LoadWAV_IO(io, true, &clip->spec, &clip->pcm, &clip->len)) {
        SDL_free(clip);
        return 0;
    }
    return zan_handle(clip);
}

ZAN_SDL_API void zan_audio_free_clip(zan_iptr clip) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr(clip);
    if (!c) return;
    /* Voices reading this clip's samples have to go first. */
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) {
        if (zan_voices[i].clip == c) zan_voice_release(&zan_voices[i]);
    }
    SDL_free(c->pcm);
    SDL_free(c);
}

ZAN_SDL_API zan_i32 zan_audio_clip_frequency(zan_iptr clip) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr(clip);
    return c ? (zan_i32)c->spec.freq : 0;
}

ZAN_SDL_API zan_i32 zan_audio_clip_channels(zan_iptr clip) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr(clip);
    return c ? (zan_i32)c->spec.channels : 0;
}

ZAN_SDL_API zan_i32 zan_audio_clip_duration_ms(zan_iptr clip) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr(clip);
    if (!c || c->spec.freq <= 0 || c->spec.channels <= 0) return 0;
    int frame = SDL_AUDIO_FRAMESIZE(c->spec);
    if (frame <= 0) return 0;
    zan_i64 frames = (zan_i64)c->len / (zan_i64)frame;
    return (zan_i32)((frames * 1000) / (zan_i64)c->spec.freq);
}

/* Starts one voice for `clip`. `loop` re-queues the clip forever (background
 * music); a one-shot voice is reaped once it has played out. Returns 0 when
 * the device is closed, the pool is full or the stream could not be created. */
ZAN_SDL_API zan_i64 zan_audio_play(zan_iptr clip, double gain, zan_i32 loop) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr(clip);
    if (!c || !zan_audio_ready) return 0;
    zan_voice_reap();
    int slot = -1;
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) {
        if (!zan_voices[i].stream) { slot = i; break; }
    }
    if (slot < 0) return 0;
    ZanVoice *v = &zan_voices[slot];
    v->clip = c;
    v->loop = loop != 0 ? 1 : 0;
    v->gen = zan_voice_gen++;
    /* A logical device per voice: SDL converts the clip's format to the
     * hardware's and mixes the logical devices together, which is what lets one
     * clip sound several times at once. */
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &c->spec,
        v->loop ? zan_voice_feed : NULL, v);
    if (!stream) {
        v->clip = NULL;
        v->loop = 0;
        return 0;
    }
    v->stream = stream;
    if (gain < 0.0) gain = 0.0;
    SDL_SetAudioStreamGain(stream, (float)gain * zan_audio_gain);
    /* Streams from SDL_OpenAudioDeviceStream start paused. */
    if (!SDL_PutAudioStreamData(stream, c->pcm, (int)c->len) ||
        !SDL_ResumeAudioStreamDevice(stream)) {
        zan_voice_release(v);
        return 0;
    }
    return zan_voice_pack((zan_i32)slot, v->gen);
}

ZAN_SDL_API zan_i32 zan_audio_voice_playing(zan_i64 voice) {
    ZanVoice *v = zan_voice_of(voice);
    if (!v) return 0;
    if (v->loop) return 1;
    if (zan_voice_pending(v) > 0) return 1;
    zan_voice_release(v);
    return 0;
}

ZAN_SDL_API void zan_audio_voice_stop(zan_i64 voice) {
    ZanVoice *v = zan_voice_of(voice);
    if (v) zan_voice_release(v);
}

ZAN_SDL_API void zan_audio_voice_set_gain(zan_i64 voice, double gain) {
    ZanVoice *v = zan_voice_of(voice);
    if (!v) return;
    if (gain < 0.0) gain = 0.0;
    SDL_SetAudioStreamGain(v->stream, (float)gain * zan_audio_gain);
}

ZAN_SDL_API void zan_audio_stop_all(void) {
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_release(&zan_voices[i]);
}

/* ===================================================================
 * Gamepads. Handles are SDL_Gamepad pointers; the joystick instance id
 * ("which") is what the ADDED/REMOVED events report.
 * =================================================================== */

ZAN_SDL_API zan_i32 zan_gamepad_init(void) {
    return zan_bool(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
}

ZAN_SDL_API zan_i32 zan_gamepad_count(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_free(ids);
    return (zan_i32)count;
}

ZAN_SDL_API zan_i32 zan_gamepad_id_at(zan_i32 index) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    zan_i32 id = 0;
    if (ids && index >= 0 && index < count) id = (zan_i32)ids[index];
    SDL_free(ids);
    return id;
}

ZAN_SDL_API zan_iptr zan_gamepad_open(zan_i32 which) {
    if (which <= 0) return 0;
    return zan_handle(SDL_OpenGamepad((SDL_JoystickID)which));
}

ZAN_SDL_API zan_iptr zan_gamepad_open_first(void) {
    zan_i32 id = zan_gamepad_id_at(0);
    return id ? zan_gamepad_open(id) : 0;
}

ZAN_SDL_API void zan_gamepad_close(zan_iptr pad) {
    if (pad) SDL_CloseGamepad((SDL_Gamepad *)zan_ptr(pad));
}

ZAN_SDL_API const char *zan_gamepad_name(zan_iptr pad) {
    const char *name = pad ? SDL_GetGamepadName((SDL_Gamepad *)zan_ptr(pad)) : NULL;
    return name ? name : "";
}

ZAN_SDL_API zan_i32 zan_gamepad_id(zan_iptr pad) {
    if (!pad) return 0;
    return (zan_i32)SDL_GetGamepadID((SDL_Gamepad *)zan_ptr(pad));
}

ZAN_SDL_API zan_i32 zan_gamepad_button(zan_iptr pad, zan_i32 button) {
    if (!pad || button < 0) return 0;
    return zan_bool(SDL_GetGamepadButton(
        (SDL_Gamepad *)zan_ptr(pad), (SDL_GamepadButton)button));
}

/* Raw axis value, -32768..32767 (triggers report 0..32767). */
ZAN_SDL_API zan_i32 zan_gamepad_axis(zan_iptr pad, zan_i32 axis) {
    if (!pad || axis < 0) return 0;
    return (zan_i32)SDL_GetGamepadAxis(
        (SDL_Gamepad *)zan_ptr(pad), (SDL_GamepadAxis)axis);
}

ZAN_SDL_API zan_i32 zan_gamepad_rumble(
    zan_iptr pad, zan_i32 low, zan_i32 high, zan_i32 milliseconds) {
    if (!pad) return 0;
    if (low < 0) low = 0;
    if (low > 65535) low = 65535;
    if (high < 0) high = 0;
    if (high > 65535) high = 65535;
    if (milliseconds < 0) milliseconds = 0;
    return zan_bool(SDL_RumbleGamepad((SDL_Gamepad *)zan_ptr(pad),
        (Uint16)low, (Uint16)high, (Uint32)milliseconds));
}

ZAN_SDL_API zan_i32 zan_sdl_event_gamepad_which(void) {
    switch (zan_last_event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_REMAPPED:
        return (zan_i32)zan_last_event.gdevice.which;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return (zan_i32)zan_last_event.gbutton.which;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return (zan_i32)zan_last_event.gaxis.which;
    default:
        return 0;
    }
}

ZAN_SDL_API zan_i32 zan_sdl_event_gamepad_button(void) {
    return (zan_i32)zan_last_event.gbutton.button;
}

ZAN_SDL_API zan_i32 zan_sdl_event_gamepad_axis(void) {
    return (zan_i32)zan_last_event.gaxis.axis;
}

ZAN_SDL_API zan_i32 zan_sdl_event_gamepad_axis_value(void) {
    return (zan_i32)zan_last_event.gaxis.value;
}

/* ===================================================================
 * Touch. Finger ids are 64-bit SDL_FingerID values; positions are
 * normalized 0..1 inside the window.
 * =================================================================== */

ZAN_SDL_API zan_i32 zan_touch_device_count(void) {
    int count = 0;
    SDL_TouchID *ids = SDL_GetTouchDevices(&count);
    SDL_free(ids);
    return (zan_i32)count;
}

ZAN_SDL_API zan_i64 zan_touch_device_at(zan_i32 index) {
    int count = 0;
    SDL_TouchID *ids = SDL_GetTouchDevices(&count);
    zan_i64 id = 0;
    if (ids && index >= 0 && index < count) id = (zan_i64)ids[index];
    SDL_free(ids);
    return id;
}

ZAN_SDL_API const char *zan_touch_device_name(zan_i64 device) {
    const char *name = SDL_GetTouchDeviceName((SDL_TouchID)device);
    return name ? name : "";
}

ZAN_SDL_API zan_i64 zan_sdl_event_finger_id(void) {
    return (zan_i64)zan_last_event.tfinger.fingerID;
}

ZAN_SDL_API zan_i64 zan_sdl_event_touch_device(void) {
    return (zan_i64)zan_last_event.tfinger.touchID;
}

ZAN_SDL_API double zan_sdl_event_finger_x(void) {
    return (double)zan_last_event.tfinger.x;
}

ZAN_SDL_API double zan_sdl_event_finger_y(void) {
    return (double)zan_last_event.tfinger.y;
}

ZAN_SDL_API double zan_sdl_event_finger_dx(void) {
    return (double)zan_last_event.tfinger.dx;
}

ZAN_SDL_API double zan_sdl_event_finger_dy(void) {
    return (double)zan_last_event.tfinger.dy;
}

ZAN_SDL_API double zan_sdl_event_finger_pressure(void) {
    return (double)zan_last_event.tfinger.pressure;
}

/* ===================================================================
 * GPU device introspection: which backend SDL picked and which shader
 * format it consumes, so a program can report (or refuse) the backend it
 * got instead of guessing from the platform.
 * =================================================================== */

ZAN_SDL_API const char *zan_gpu_driver_name(zan_iptr handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->device) return "";
    const char *name = SDL_GetGPUDeviceDriver(ctx->device);
    return name ? name : "";
}

ZAN_SDL_API const char *zan_gpu_shader_format(zan_iptr handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->device) return "";
    SDL_GPUShaderFormat f = SDL_GetGPUShaderFormats(ctx->device);
    if (f & SDL_GPU_SHADERFORMAT_MSL) return "msl";
    if (f & SDL_GPU_SHADERFORMAT_SPIRV) return "spirv";
    if (f & SDL_GPU_SHADERFORMAT_DXIL) return "dxil";
    if (f & SDL_GPU_SHADERFORMAT_METALLIB) return "metallib";
    if (f & SDL_GPU_SHADERFORMAT_PRIVATE) return "private";
    return "invalid";
}


/* ===================================================================
 * SDL_Renderer sprite batch (SdlSpriteBatch).
 *
 * The immediate-mode SdlRenderer issues one native call per sprite; a
 * 2D game drawing thousands of textured quads per frame pays driver
 * overhead for each. This bridge accumulates quads on the CPU in
 * memory that is reused across frames (grow-only), then End() submits
 * one SDL_RenderGeometryRaw per *run* of same-texture quads. Painter
 * order is preserved -- runs are only merged between adjacent quads
 * sharing a texture, so translucent layering can never reorder.
 *
 * Frame model:
 *   b = zan_sb_new(renderer, capacity)
 *   loop:
 *     zan_sb_begin(b)                    // reset counters, keep memory
 *     zan_sb_queue(b, tex, dst, src, tint, clip)   // N times
 *     zan_sb_end(b)                      // submit runs
 *
 * Coordinates are in *renderer* space (the Zan side applies the same
 * Sc() scaling as immediate mode before queueing). Clip state is
 * captured per quad at queue time: SDL_RenderGeometryRaw honors
 * SDL_SetRenderClipRect, so End() switches the clip only at run
 * boundaries and restores the caller's state afterwards.
 * =================================================================== */

typedef struct { float x, y; SDL_FColor col; float u, v; } ZanSbVertex;
typedef struct { SDL_Texture *tex; int first; int count; } ZanSbRun;
typedef struct {
    SDL_Texture *tex;
    float dx, dy, dw, dh;     /* destination rect */
    float u0, v0, u1, v1;     /* normalized source rect */
    float r, g, b, a;         /* tint */
    int clip;                 /* 0 = unclipped, 1 = clip active */
    float cl, ct, cr2, cb2;   /* clip rect when clip == 1 */
} ZanSbCmd;

typedef struct {
    SDL_Renderer *ren;
    ZanSbCmd *cmds;  int ccount, ccap;
    ZanSbVertex *verts; int vcap;      /* worst case 6 * ccap */
    ZanSbRun *runs;    int rcount, rcap;
    /* statistics readable after End() */
    int queued;      /* sprites accepted this frame */
    int vertices;    /* vertices submitted this frame */
    int runs_used;   /* draw calls submitted this frame */
    int dropped;     /* sprites skipped (fully clipped / invalid) */
} ZanSbCtx;

ZAN_SDL_API zan_iptr zan_sb_new(zan_iptr renderer, zan_i32 capacity) {
    SDL_Renderer *ren = (SDL_Renderer *)zan_ptr(renderer);
    if (!ren) return 0;
    if (capacity < 64) capacity = 64;
    ZanSbCtx *b = (ZanSbCtx *)SDL_calloc(1, sizeof(ZanSbCtx));
    if (!b) return 0;
    b->ren = ren;
    b->ccap = capacity;
    b->vcap = capacity * 6;
    b->cmds = (ZanSbCmd *)SDL_malloc(sizeof(ZanSbCmd) * (size_t)b->ccap);
    b->verts = (ZanSbVertex *)SDL_malloc(sizeof(ZanSbVertex) * (size_t)b->vcap);
    b->rcap = 16;
    b->runs = (ZanSbRun *)SDL_malloc(sizeof(ZanSbRun) * (size_t)b->rcap);
    if (!b->cmds || !b->verts || !b->runs) {
        SDL_free(b->cmds); SDL_free(b->verts); SDL_free(b->runs);
        SDL_free(b);
        return 0;
    }
    return zan_handle(b);
}

ZAN_SDL_API void zan_sb_free(zan_iptr handle) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    if (!b) return;
    SDL_free(b->cmds);
    SDL_free(b->verts);
    SDL_free(b->runs);
    SDL_free(b);
}

ZAN_SDL_API void zan_sb_begin(zan_iptr handle) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    if (!b) return;
    b->ccount = 0;
    b->queued = 0; b->vertices = 0; b->runs_used = 0; b->dropped = 0;
}

/* Queue one textured quad. Coordinates are already in renderer space;
 * source rect (sx, sy, sw, sh) is in texture pixels, sw/sh <= 0 means
 * the whole texture. clip = (-1, ...) disables clipping for this quad;
 * otherwise the rect is the renderer-space clip the quad must respect. */
ZAN_SDL_API void zan_sb_queue(
    zan_iptr handle, zan_iptr texture,
    zan_i32 dx, zan_i32 dy, zan_i32 dw, zan_i32 dh,
    zan_i32 sx, zan_i32 sy, zan_i32 sw, zan_i32 sh,
    zan_i32 r, zan_i32 g, zan_i32 bl, zan_i32 a,
    zan_i32 clip_x, zan_i32 clip_y, zan_i32 clip_w, zan_i32 clip_h) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    SDL_Texture *tex = (SDL_Texture *)zan_ptr(texture);
    if (!b || !tex) return;
    if (dw <= 0 || dh <= 0) { b->dropped++; return; }

    int clipped = clip_x >= 0;
    if (clipped) {
        if (dx + dw <= (float)clip_x || (float)dx >= (float)(clip_x + clip_w) ||
            dy + dh <= (float)clip_y || (float)dy >= (float)(clip_y + clip_h)) {
            b->dropped++;
            return;
        }
        if ((float)clip_x <= dx && (float)clip_y <= dy &&
            (float)(clip_x + clip_w) >= dx + dw &&
            (float)(clip_y + clip_h) >= dy + dh) {
            clipped = 0;   /* clip covers the quad entirely */
        }
    }

    if (b->ccount + 1 > b->ccap) {
        int ncap = b->ccap * 2;
        ZanSbCmd *nc = (ZanSbCmd *)SDL_realloc(b->cmds, sizeof(ZanSbCmd) * (size_t)ncap);
        ZanSbVertex *nv = (ZanSbVertex *)SDL_realloc(b->verts, sizeof(ZanSbVertex) * (size_t)ncap * 6);
        if (!nc || !nv) {
            if (nc) { b->cmds = nc; }
            if (nv) { b->verts = nv; }
            b->dropped++;
            return;
        }
        b->cmds = nc; b->verts = nv; b->ccap = ncap; b->vcap = ncap * 6;
    }
    ZanSbCmd *c = &b->cmds[b->ccount];
    c->tex = tex;
    c->dx = (float)dx; c->dy = (float)dy; c->dw = (float)dw; c->dh = (float)dh;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (sw > 0 && sh > 0) {
        float fw = 0.0f, fh = 0.0f;
        if (SDL_GetTextureSize(tex, &fw, &fh) && fw > 0.0f && fh > 0.0f) {
            u0 = (float)sx / fw; v0 = (float)sy / fh;
            u1 = (float)(sx + sw) / fw; v1 = (float)(sy + sh) / fh;
        }
    }
    c->u0 = u0; c->v0 = v0; c->u1 = u1; c->v1 = v1;
    c->r = (float)r / 255.0f; c->g = (float)g / 255.0f;
    c->b = (float)bl / 255.0f; c->a = (float)a / 255.0f;
    c->clip = clipped;
    if (clipped) {
        c->cl = (float)clip_x; c->ct = (float)clip_y;
        c->cr2 = (float)(clip_x + clip_w); c->cb2 = (float)(clip_y + clip_h);
    }
    b->ccount++;
    b->queued++;
}

ZAN_SDL_API zan_i32 zan_sb_end(zan_iptr handle) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    if (!b) return 0;
    /* Build vertices + runs. Runs merge adjacent quads with the same
     * texture AND the same clip state (a clip change must flush so the
     * renderer's clip rect can follow). */
    int vused = 0;
    b->rcount = 0;
    for (int i = 0; i < b->ccount; i++) {
        ZanSbCmd *c = &b->cmds[i];
        float x0 = c->dx, y0 = c->dy, x1 = c->dx + c->dw, y1 = c->dy + c->dh;
        ZanSbVertex *v = &b->verts[vused];
        v[0].x = x0; v[0].y = y0; v[0].col.r = c->r; v[0].col.g = c->g; v[0].col.b = c->b; v[0].col.a = c->a;
        v[0].u = c->u0; v[0].v = c->v0;
        v[1].x = x1; v[1].y = y0; v[1].col.r = c->r; v[1].col.g = c->g; v[1].col.b = c->b; v[1].col.a = c->a;
        v[1].u = c->u1; v[1].v = c->v0;
        v[2].x = x0; v[2].y = y1; v[2].col.r = c->r; v[2].col.g = c->g; v[2].col.b = c->b; v[2].col.a = c->a;
        v[2].u = c->u0; v[2].v = c->v1;
        v[3] = v[2];
        v[4] = v[1];
        v[5].x = x1; v[5].y = y1; v[5].col.r = c->r; v[5].col.g = c->g; v[5].col.b = c->b; v[5].col.a = c->a;
        v[5].u = c->u1; v[5].v = c->v1;

        int merged = 0;
        if (b->rcount > 0) {
            ZanSbRun *last = &b->runs[b->rcount - 1];
            ZanSbCmd *first = &b->cmds[last->first / 6];
            if (last->tex == c->tex && first->clip == c->clip) {
                last->count += 6;
                merged = 1;
            }
        }
        if (!merged) {
            if (b->rcount + 1 > b->rcap) {
                b->rcap *= 2;
                ZanSbRun *nr = (ZanSbRun *)SDL_realloc(b->runs, sizeof(ZanSbRun) * (size_t)b->rcap);
                if (!nr) { vused += 6; continue; }
                b->runs = nr;
            }
            ZanSbRun *run = &b->runs[b->rcount++];
            run->tex = c->tex; run->first = vused; run->count = 6;
        }
        vused += 6;
    }
    /* Submit per run, switching the clip only when it changes. */
    int cur_clip = -1;
    int ok = 1;
    for (int i = 0; i < b->rcount; i++) {
        ZanSbRun *run = &b->runs[i];
        ZanSbCmd *first = &b->cmds[run->first / 6];
        if (first->clip != cur_clip) {
            SDL_Rect rect;
            if (first->clip) {
                rect.x = (int)first->cl; rect.y = (int)first->ct;
                rect.w = (int)(first->cr2 - first->cl);
                rect.h = (int)(first->cb2 - first->ct);
                SDL_SetRenderClipRect(b->ren, &rect);
            } else {
                SDL_SetRenderClipRect(b->ren, NULL);
            }
            cur_clip = first->clip;
        }
        if (!SDL_RenderGeometryRaw(b->ren, run->tex,
                &b->verts[run->first].x, (int)sizeof(ZanSbVertex),
                &b->verts[run->first].col, (int)sizeof(ZanSbVertex),
                &b->verts[run->first].u, (int)sizeof(ZanSbVertex),
                run->count, NULL, 0, 0)) {
            ok = 0;
        }
    }
    /* The batch never leaves the renderer clipped: immediate-mode calls
     * that follow must not inherit a clip the batch set internally. */
    if (cur_clip != -1) { SDL_SetRenderClipRect(b->ren, NULL); }
    b->vertices = vused;
    b->runs_used = b->rcount;
    return ok;
}

/* Statistics after End(), two fields packed per call:
 * stats_a = (vertices << 16) | queued, stats_b = (dropped << 16) | runs. */
ZAN_SDL_API zan_i32 zan_sb_stats_a(zan_iptr handle) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    if (!b) return 0;
    return (zan_i32)((b->queued & 0xFFFF) | ((b->vertices & 0xFFFF) << 16));
}
ZAN_SDL_API zan_i32 zan_sb_stats_b(zan_iptr handle) {
    ZanSbCtx *b = (ZanSbCtx *)zan_ptr(handle);
    if (!b) return 0;
    return (zan_i32)((b->runs_used & 0xFFFF) | ((b->dropped & 0xFFFF) << 16));
}
