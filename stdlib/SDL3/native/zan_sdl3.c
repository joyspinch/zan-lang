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

ZAN_SDL_API double zan_sdl_window_to_render_x(
    zan_i64 renderer, double window_x, double window_y) {
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

ZAN_SDL_API double zan_sdl_window_to_render_y(
    zan_i64 renderer, double window_x, double window_y) {
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

ZAN_SDL_API zan_i64 zan_sdl_set_draw_color(
    zan_i64 renderer, zan_i64 red, zan_i64 green, zan_i64 blue, zan_i64 alpha) {
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
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
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

ZAN_SDL_API zan_i64 zan_sdl_load_bmp_texture_colorkey(
    zan_i64 renderer,
    const char *path,
    zan_i64 red,
    zan_i64 green,
    zan_i64 blue) {
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

ZAN_SDL_API zan_i64 zan_sdl_load_image_texture(
    zan_i64 renderer,
    const char *path) {
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
    if (nearest >= 256)
        return zan_bool(SDL_SetTextureAlphaMod(
            (SDL_Texture *)zan_ptr(texture), (uint8_t)(nearest - 256)));
    SDL_ScaleMode mode = nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
    return zan_bool(SDL_SetTextureScaleMode((SDL_Texture *)zan_ptr(texture), mode));
}

ZAN_SDL_API zan_i64 zan_sdl_render_texture(
    zan_i64 renderer, zan_i64 texture,
    zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
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

/* ---- 2D render backend extensions (render targets, blend, clip) ----
 * These thin wrappers expose the SDL_Renderer features a GPU-accelerated GUI
 * canvas needs: offscreen render targets (for downsample blur), source-region
 * texture draws (atlas/scaled blit), scissor clipping and blend modes. They
 * work on every SDL_Renderer backend (D3D11/D3D12/Metal/Vulkan/OpenGL) with no
 * custom shaders, so the cross-platform GPU path is active today. */

static SDL_BlendMode zan_blend_mode(zan_i64 mode) {
    if (mode == 0) return SDL_BLENDMODE_NONE;
    if (mode == 2) return SDL_BLENDMODE_ADD;
    if (mode == 3) return SDL_BLENDMODE_MOD;
    return SDL_BLENDMODE_BLEND;
}

ZAN_SDL_API zan_i64 zan_sdl_create_target_texture(
    zan_i64 renderer, zan_i64 width, zan_i64 height) {
    if (!renderer) return 0;
    SDL_Texture *texture = SDL_CreateTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_TARGET,
        (int)width, (int)height);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return zan_handle(texture);
}

ZAN_SDL_API zan_i64 zan_sdl_set_render_target(zan_i64 renderer, zan_i64 texture) {
    if (!renderer) return 0;
    SDL_Texture *tex = texture ? (SDL_Texture *)zan_ptr(texture) : NULL;
    return zan_bool(SDL_SetRenderTarget((SDL_Renderer *)zan_ptr(renderer), tex));
}

ZAN_SDL_API zan_i64 zan_sdl_render_texture_region(
    zan_i64 renderer, zan_i64 texture,
    zan_i64 sx, zan_i64 sy, zan_i64 sw, zan_i64 sh,
    zan_i64 dx, zan_i64 dy, zan_i64 dw, zan_i64 dh) {
    if (!renderer || !texture) return 0;
    SDL_FRect src = zan_rect(sx, sy, sw, sh);
    SDL_FRect dst = zan_rect(dx, dy, dw, dh);
    return zan_bool(SDL_RenderTexture(
        (SDL_Renderer *)zan_ptr(renderer),
        (SDL_Texture *)zan_ptr(texture), &src, &dst));
}

ZAN_SDL_API zan_i64 zan_sdl_set_texture_alpha(zan_i64 texture, zan_i64 alpha) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureAlphaMod(
        (SDL_Texture *)zan_ptr(texture), (Uint8)alpha));
}

ZAN_SDL_API zan_i64 zan_sdl_set_texture_color(
    zan_i64 texture, zan_i64 red, zan_i64 green, zan_i64 blue) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureColorMod(
        (SDL_Texture *)zan_ptr(texture), (Uint8)red, (Uint8)green, (Uint8)blue));
}

ZAN_SDL_API zan_i64 zan_sdl_set_texture_blend(zan_i64 texture, zan_i64 mode) {
    if (!texture) return 0;
    return zan_bool(SDL_SetTextureBlendMode(
        (SDL_Texture *)zan_ptr(texture), zan_blend_mode(mode)));
}

ZAN_SDL_API zan_i64 zan_sdl_set_draw_blend(zan_i64 renderer, zan_i64 mode) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderDrawBlendMode(
        (SDL_Renderer *)zan_ptr(renderer), zan_blend_mode(mode)));
}

ZAN_SDL_API zan_i64 zan_sdl_set_render_clip(
    zan_i64 renderer, zan_i64 x, zan_i64 y, zan_i64 width, zan_i64 height) {
    if (!renderer) return 0;
    SDL_Rect rect;
    rect.x = (int)x; rect.y = (int)y; rect.w = (int)width; rect.h = (int)height;
    return zan_bool(SDL_SetRenderClipRect((SDL_Renderer *)zan_ptr(renderer), &rect));
}

ZAN_SDL_API zan_i64 zan_sdl_clear_render_clip(zan_i64 renderer) {
    if (!renderer) return 0;
    return zan_bool(SDL_SetRenderClipRect((SDL_Renderer *)zan_ptr(renderer), NULL));
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

/* ===================================================================
 * SDL_GPU sprite-batch renderer.
 *
 * Higher-level Game.Core/Game.Render code binds these through
 * [DllImport("zan_sdl3")]. The frame model is:
 *   ctx = zan_gpu_create(window)
 *   loop:
 *     zan_gpu_begin(ctx, clear rgba)
 *     zan_gpu_draw(ctx, tex, dst rect, uv rect, tint rgba)  // batched
 *     zan_gpu_end(ctx)                                       // upload+submit
 *
 * Draws are accumulated on the CPU and flushed in one render pass, grouped
 * into runs of the same texture to minimize state changes. Vertex positions
 * are in framebuffer pixels; the shader maps them to NDC via the drawable
 * size pushed as a vertex uniform.
 *
 * Shaders are provided as Metal Shading Language, so the GPU path is active
 * on the Metal backend today. SPIR-V (Vulkan/Linux) and DXIL (D3D12/Windows)
 * variants can be added to enable those backends; until then zan_gpu_create
 * returns 0 on non-Metal drivers and callers fall back to the 2D renderer.
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
/* A loaded texture carries its pixel size so draws can specify source
 * rectangles in pixels (atlas-friendly) and the bridge derives UVs. */
typedef struct { SDL_GPUTexture *tex; int w, h; } ZanGpuTex;

typedef struct {
    SDL_GPUDevice *device;
    SDL_Window *window;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *vbuf;
    Uint32 vbuf_cap;                 /* capacity in vertices */
    ZanGpuVertex *verts; int vcount, vcap;
    ZanGpuDraw *draws;   int dcount, dcap;
    SDL_GPUCommandBuffer *cmd;       /* per-frame */
    SDL_GPUTexture *swap;            /* per-frame swapchain image */
    Uint32 fb_w, fb_h;
    float cr, cg, cb, ca;
    /* Offscreen mode: render into a fixed target and read pixels back on the
     * CPU. Enables headless GPU verification with no window/display server. */
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

/* Build the sprite pipeline for the given color target format (swapchain
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

ZAN_SDL_API zan_i64 zan_gpu_create(zan_i64 window) {
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

/* Windowless GPU context that renders into a WxH target read back on the CPU;
 * for headless verification and offscreen composition. */
ZAN_SDL_API zan_i64 zan_gpu_create_offscreen(zan_i64 width, zan_i64 height) {
    if (width <= 0 || height <= 0) return 0;
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
    dn.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; dn.size = ctx->off_w * ctx->off_h * 4;
    ctx->readbuf = SDL_CreateGPUTransferBuffer(dev, &dn);
    ctx->cpu = (unsigned char *)SDL_calloc(1, ctx->off_w * ctx->off_h * 4);
    return zan_handle(ctx);
}

/* Read one pixel from the last offscreen frame, packed as 0xRRGGBBAA. */
ZAN_SDL_API zan_i64 zan_gpu_read_pixel(zan_i64 handle, zan_i64 x, zan_i64 y) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->offscreen || !ctx->cpu) return 0;
    if (x < 0 || y < 0 || (Uint32)x >= ctx->off_w || (Uint32)y >= ctx->off_h) return 0;
    unsigned char *p = ctx->cpu + ((size_t)((Uint32)y * ctx->off_w + (Uint32)x)) * 4;
    return ((zan_i64)p[0] << 24) | ((zan_i64)p[1] << 16) |
           ((zan_i64)p[2] << 8) | (zan_i64)p[3];
}

ZAN_SDL_API void zan_gpu_destroy(zan_i64 handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx) return;
    if (ctx->vbuf) SDL_ReleaseGPUBuffer(ctx->device, ctx->vbuf);
    if (ctx->sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->sampler);
    if (ctx->pipeline) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->pipeline);
    if (ctx->offtex) SDL_ReleaseGPUTexture(ctx->device, ctx->offtex);
    if (ctx->readbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->readbuf);
    if (ctx->window) SDL_ReleaseWindowFromGPUDevice(ctx->device, ctx->window);
    if (ctx->device) SDL_DestroyGPUDevice(ctx->device);
    SDL_free(ctx->cpu); SDL_free(ctx->verts); SDL_free(ctx->draws); SDL_free(ctx);
}

/* Create a sampler texture from tightly packed RGBA32 pixels. */
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

    Uint32 bytes = (Uint32)(width * height * 4);
    SDL_GPUTransferBufferCreateInfo up;
    memset(&up, 0, sizeof(up));
    up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; up.size = bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(ctx->device, &up);
    void *map = SDL_MapGPUTransferBuffer(ctx->device, tb, false);
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

ZAN_SDL_API zan_i64 zan_gpu_load_texture(
    zan_i64 handle, zan_i64 width, zan_i64 height, const void *pixels) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    return zan_gpu_wrap(zan_gpu_make_texture(ctx, (int)width, (int)height, pixels),
                        (int)width, (int)height);
}

/* Solid white WxH texture; tint it via zan_gpu_draw color to get any color. */
ZAN_SDL_API zan_i64 zan_gpu_solid_texture(zan_i64 handle, zan_i64 width, zan_i64 height) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || width <= 0 || height <= 0) return 0;
    size_t bytes = (size_t)(width * height * 4);
    unsigned char *px = (unsigned char *)SDL_malloc(bytes);
    memset(px, 0xFF, bytes);
    SDL_GPUTexture *tex = zan_gpu_make_texture(ctx, (int)width, (int)height, px);
    SDL_free(px);
    return zan_gpu_wrap(tex, (int)width, (int)height);
}

/* Load a BMP file as an RGBA sampler texture. */
ZAN_SDL_API zan_i64 zan_gpu_load_bmp(zan_i64 handle, const char *path) {
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
ZAN_SDL_API zan_i64 zan_gpu_load_image(zan_i64 handle, const char *path) {
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

ZAN_SDL_API void zan_gpu_free_texture(zan_i64 handle, zan_i64 texture) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    if (ctx && t) {
        if (t->tex) SDL_ReleaseGPUTexture(ctx->device, t->tex);
        SDL_free(t);
    }
}

ZAN_SDL_API zan_i64 zan_gpu_texture_width(zan_i64 texture) {
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    return t ? t->w : 0;
}

ZAN_SDL_API zan_i64 zan_gpu_texture_height(zan_i64 texture) {
    ZanGpuTex *t = (ZanGpuTex *)zan_ptr(texture);
    return t ? t->h : 0;
}

ZAN_SDL_API zan_i64 zan_gpu_begin(
    zan_i64 handle, zan_i64 r, zan_i64 g, zan_i64 b, zan_i64 a) {
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
 * texture) into the destination rect (dx,dy,dw,dh in framebuffer pixels),
 * multiplied by the r,g,b,a tint (0-255). */
ZAN_SDL_API void zan_gpu_draw(
    zan_i64 handle, zan_i64 texture,
    zan_i64 dx, zan_i64 dy, zan_i64 dw, zan_i64 dh,
    zan_i64 sx, zan_i64 sy, zan_i64 sw, zan_i64 sh,
    zan_i64 r, zan_i64 g, zan_i64 b, zan_i64 a) {
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

ZAN_SDL_API void zan_gpu_end(zan_i64 handle) {
    ZanGpuCtx *ctx = (ZanGpuCtx *)zan_ptr(handle);
    if (!ctx || !ctx->cmd) return;
    if (!ctx->swap) { SDL_SubmitGPUCommandBuffer(ctx->cmd); ctx->cmd = NULL; return; }

    SDL_GPUTransferBuffer *tb = NULL;
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
        SDL_GPUTransferBufferCreateInfo up;
        memset(&up, 0, sizeof(up));
        up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; up.size = bytes;
        tb = SDL_CreateGPUTransferBuffer(ctx->device, &up);
        void *map = SDL_MapGPUTransferBuffer(ctx->device, tb, false);
        memcpy(map, ctx->verts, bytes);
        SDL_UnmapGPUTransferBuffer(ctx->device, tb);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(ctx->cmd);
        SDL_GPUTransferBufferLocation loc;
        memset(&loc, 0, sizeof(loc));
        loc.transfer_buffer = tb; loc.offset = 0;
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
    if (tb) SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
    ctx->cmd = NULL; ctx->swap = NULL;
}
