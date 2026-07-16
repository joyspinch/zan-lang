/*
 * gui_sdl_smoke.c — cross-platform SDL3 windowing smoke for the zan_gui runtime.
 *
 * Proves the migration backbone WITHOUT touching the working IDE: it opens an
 * SDL3 window (D3D11/Metal/Vulkan/GL, chosen by SDL per-platform), drives the
 * existing zan_gui *software* rasterizer to fill a CPU surface, then uploads
 * that surface to a streaming texture and presents it via the GPU. Window,
 * events (mouse/keyboard/resize/close) and present all go through SDL, i.e. the
 * single cross-platform path that will replace the split Win32/X11 code.
 *
 * Build (Windows, from repo root, after build\zan_gui.lib exists):
 *   see scripts/build_gui_sdl_smoke.ps1
 * Build (Linux):
 *   cc tests/runtime/gui_sdl_smoke.c -o build/gui_sdl_smoke \
 *      $(pkg-config --cflags --libs sdl3) -Lbuild -lzan_gui -lm
 */

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

typedef int64_t i64;

/* zan_gui software rasterizer (build\zan_gui.lib / libzan_gui) */
extern i64  zan_gui_create_surface(i64 width, i64 height);
extern i64  zan_gui_destroy_surface(i64 id);
extern i64  zan_gui_surface_width(i64 id);
extern i64  zan_gui_surface_height(i64 id);
extern void zan_gui_clear(i64 surface_id, i64 color);
extern void zan_gui_fill_rect(i64 s, i64 x, i64 y, i64 w, i64 h, i64 color);
extern void zan_gui_fill_rounded_rect(i64 s, i64 x, i64 y, i64 w, i64 h, i64 r, i64 color);
extern void zan_gui_fill_vgrad(i64 s, i64 x, i64 y, i64 w, i64 h, i64 top, i64 bot);
extern void zan_gui_draw_rect(i64 s, i64 x, i64 y, i64 w, i64 h, i64 color, i64 th);
extern void zan_gui_blur_rect(i64 s, i64 x, i64 y, i64 w, i64 h, i64 radius);
extern void zan_gui_draw_text(i64 s, i64 x, i64 y, const char *text, i64 color, i64 size);
extern void *zan_gui_get_pixels(i64 surface_id);

static i64 argb(int a, int r, int g, int b) {
    return ((i64)(a & 0xFF) << 24) | ((i64)(r & 0xFF) << 16) |
           ((i64)(g & 0xFF) << 8)  | (i64)(b & 0xFF);
}

/* Draw a demo frame into the software surface. `mx` follows the mouse so we can
 * eyeball latency/hover; `t` animates a moving glass panel over a gradient. */
static void draw_scene(i64 surf, int w, int h, int mx, int my, double t) {
    zan_gui_clear(surf, argb(255, 24, 26, 32));
    zan_gui_fill_vgrad(surf, 0, 0, w, h, argb(255, 40, 52, 92), argb(255, 18, 20, 30));

    /* animated content behind the glass */
    for (int i = 0; i < 6; i++) {
        int cx = (int)(w * (0.15 + 0.12 * i) + 60.0 * sin(t + i));
        int cy = (int)(h * 0.5 + 120.0 * cos(t * 0.8 + i));
        int col = argb(255, 80 + i * 25, 200 - i * 20, 255 - i * 10);
        zan_gui_fill_rounded_rect(surf, cx - 45, cy - 45, 90, 90, 24, col);
    }

    /* frosted glass panel — software blur of the backdrop */
    int px = w / 2 - 220, py = h / 2 - 130, pw = 440, ph = 260;
    zan_gui_blur_rect(surf, px, py, pw, ph, 18);
    zan_gui_fill_rounded_rect(surf, px, py, pw, ph, 20, argb(70, 255, 255, 255));
    zan_gui_draw_rect(surf, px, py, pw, ph, argb(90, 255, 255, 255), 1);
    zan_gui_draw_text(surf, px + 28, py + 28, "SDL3 GUI backend — cross platform", argb(255, 245, 247, 255), 20);
    zan_gui_draw_text(surf, px + 28, py + 68, "software raster surface -> GPU texture -> present", argb(220, 210, 220, 245), 15);
    char pos[96];
    snprintf(pos, sizeof(pos), "window %dx%d   mouse %d,%d", w, h, mx, my);
    zan_gui_draw_text(surf, px + 28, py + 104, pos, argb(200, 180, 210, 255), 14);

    /* hover dot: immediate feedback so latency is visible */
    zan_gui_fill_rounded_rect(surf, mx - 10, my - 10, 20, 20, 10, argb(230, 120, 235, 200));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    int w = 1000, h = 700;
    SDL_Window *win = SDL_CreateWindow("zan_gui + SDL3 smoke", w, h, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(ren, 1);
    printf("renderer=%s\n", SDL_GetRendererName(ren));

    i64 surf = zan_gui_create_surface(w, h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);

    int running = 1, mx = w / 2, my = h / 2;
    Uint64 t0 = SDL_GetTicks();
    int frames = 0; Uint64 fpsT = t0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
            else if (e.type == SDL_EVENT_MOUSE_MOTION) { mx = (int)e.motion.x; my = (int)e.motion.y; }
            else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                w = e.window.data1; h = e.window.data2;
                zan_gui_destroy_surface(surf);
                surf = zan_gui_create_surface(w, h);
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
            }
        }
        double t = (SDL_GetTicks() - t0) / 1000.0;
        draw_scene(surf, w, h, mx, my, t);

        void *pixels = zan_gui_get_pixels(surf);
        SDL_UpdateTexture(tex, NULL, pixels, w * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        frames++;
        Uint64 now = SDL_GetTicks();
        if (now - fpsT >= 1000) {
            printf("fps=%d size=%dx%d\n", frames, w, h);
            fflush(stdout);
            frames = 0; fpsT = now;
            /* headless/CI: exit after a few seconds if requested via arg */
        }
        if (argc > 1 && (now - t0) > 3000) running = 0; /* smoke mode: auto-exit */
    }

    zan_gui_destroy_surface(surf);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
