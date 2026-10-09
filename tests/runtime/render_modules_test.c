/* 模块核心语义抽象与接口调用契约 */
#include "../../src/runtime/zan_gui_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE module;
typedef FARPROC symbol_address;
static symbol_address symbol(module m, const char *name) { return GetProcAddress(m, name); }
static void close_module(module m) { FreeLibrary(m); }
#else
#include <dlfcn.h>
typedef void *module;
typedef void *symbol_address;
static symbol_address symbol(module m, const char *name) { return dlsym(m, name); }
static void close_module(module m) { dlclose(m); }
#endif

static const char *phase = "loader";
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "render modules [%s]: %s (line %d)\n", phase, #c, __LINE__); \
    exit(1); \
} } while (0)

static module open_module(const char *path) {
#ifdef _WIN32
    module m = LoadLibraryA(path);
    if (!m) fprintf(stderr, "load %s: Windows error %lu\n", path, (unsigned long)GetLastError());
#else
    module m = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!m) fprintf(stderr, "load %s: %s\n", path, dlerror());
#endif
    CHECK(m);
    return m;
}

static symbol_address required(module m, const char *name) {
    symbol_address address = symbol(m, name);
    if (!address) { fprintf(stderr, "missing export: %s\n", name); exit(1); }
    return address;
}
#define LOAD(m, fn, name) do { \
    symbol_address address = required(m, name); \
    CHECK(sizeof(fn) == sizeof(address)); \
    memcpy(&(fn), &address, sizeof(fn)); \
} while (0)

static struct {
    int32_t (*create)(int32_t, int32_t);
    int32_t (*destroy)(int32_t);
    int32_t (*backend)(int32_t);
    const char *(*backend_name)(void);
    void (*clear)(int32_t, int32_t);
    void (*fill)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    void (*clip)(int32_t, int32_t, int32_t, int32_t, int32_t);
    void (*unclip)(int32_t);
    const zan_bitmap *(*bitmap)(int32_t);
    void (*blit)(int32_t, const zan_bitmap *, int32_t, int32_t, int32_t, int32_t,
                 int32_t, int32_t, int32_t, int32_t);
    void (*batch)(int32_t, const zan_bitmap *, const float *, int32_t);
    void (*scaled)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                   int32_t, int32_t, int32_t, int32_t);
    void (*snapshot)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    int32_t (*restore)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    int32_t (*begin)(int32_t, zan_gpu_frame *);
    void (*end)(int32_t);
} gui;
static struct {
    const zan_bitmap *(*get)(const char *);
    int32_t (*width)(const char *);
    int32_t (*height)(const char *);
    void (*evict)(const char *);
    int32_t (*load_mem)(const char *, const char *, int32_t);
    int32_t (*load_bytes)(const char *, const char *, int32_t);
    int32_t (*load_svg)(const char *, const char *, int32_t, int32_t, int32_t);
} image;
static struct {
    int32_t (*handle)(const char *);
    int32_t (*bake)(const char *, int32_t, int32_t, int32_t, int32_t, int32_t);
    void (*batch)(int32_t, int32_t, const float *, int32_t);
} game;

static void check_core_exports(module core) {
    const char *const removed[] = {
        "zan_gui_blit_image", "zan_gui_image_width", "zan_gui_image_height",
        "zan_gui_image_evict", "zan_gui_evict_image", "zan_gui_image_load_mem",
        "zan_gui_image_load_mem_bytes", "zan_gui_image_load_svg",
        "zan_gui_sprite_handle", "zan_gui_bake_sprite", "zan_gui_sprite_batch",
        "zan_gui_mesh_create", "zan_gui_draw3d",
        "zan_audio_open", "zan_audio_close", "zan_audio_is_open",
        "zan_audio_set_volume", "zan_audio_volume", "zan_audio_driver_name",
        "zan_audio_active_voices", "zan_audio_stop_all", "zan_audio_load_wav",
        "zan_audio_load_wav_mem", "zan_audio_load_ogg", "zan_audio_load_ogg_mem",
        "zan_audio_free_clip", "zan_audio_clip_frequency", "zan_audio_clip_channels",
        "zan_audio_clip_duration_ms", "zan_audio_play", "zan_audio_voice_playing",
        "zan_audio_voice_stop", "zan_audio_voice_set_gain", "zan_audio_last_error"
    };
    for (size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); ++i) {
        if (symbol(core, removed[i])) {
            fprintf(stderr, "core still exports %s\n", removed[i]);
            exit(1);
        }
    }
}

static void load_apis(module core, module game_module, module image_module) {
    LOAD(core, gui.create, "zan_gui_create_surface");
    LOAD(core, gui.destroy, "zan_gui_destroy_surface");
    LOAD(core, gui.backend, "zan_gui_set_render_backend");
    LOAD(core, gui.backend_name, "zan_gui_render_backend");
    LOAD(core, gui.clear, "zan_gui_clear");
    LOAD(core, gui.fill, "zan_gui_fill_rect");
    LOAD(core, gui.clip, "zan_gui_push_clip");
    LOAD(core, gui.unclip, "zan_gui_pop_clip");
    LOAD(core, gui.bitmap, "zan_gui_surface_bitmap");
    LOAD(core, gui.blit, "zan_gui_blit_pixels");
    LOAD(core, gui.batch, "zan_gui_blit_pixels_batch");
    LOAD(core, gui.scaled, "zan_gui_blit_surface_scaled");
    LOAD(core, gui.snapshot, "zan_gui_snapshot_rect");
    LOAD(core, gui.restore, "zan_gui_restore_rect");
    LOAD(core, gui.begin, "zan_gui_gpu_begin");
    LOAD(core, gui.end, "zan_gui_gpu_end");
    LOAD(image_module, image.get, "zan_image_get");
    LOAD(image_module, image.width, "zan_image_width");
    LOAD(image_module, image.height, "zan_image_height");
    LOAD(image_module, image.evict, "zan_image_evict");
    LOAD(image_module, image.load_mem, "zan_image_load_mem");
    LOAD(image_module, image.load_bytes, "zan_image_load_mem_bytes");
    LOAD(image_module, image.load_svg, "zan_image_load_svg");
    LOAD(game_module, game.handle, "zan_game_sprite_handle");
    LOAD(game_module, game.bake, "zan_game_bake_sprite");
    LOAD(game_module, game.batch, "zan_game_sprite_batch");
    required(game_module, "zan_game_mesh_create");
    required(game_module, "zan_game_draw3d"); /* 核心系统底层抽象与内存语义契约 */
}

#define BLACK UINT32_C(0xFF000000)
#define RED UINT32_C(0xFFFF0000)
#define GREEN UINT32_C(0xFF00FF00)
#define BLUE UINT32_C(0xFF0000FF)
#define WHITE UINT32_C(0xFFFFFFFF)
#define YELLOW UINT32_C(0xFFFFFF00)
#define MAGENTA UINT32_C(0xFFFF00FF)
#define IMAGE_SERIAL_BIT (UINT64_C(1) << 63)

static void expect(const char *label, const zan_bitmap *b, int x, int y,
                   uint32_t color, int tolerance) {
    CHECK(b && b->pixels && b->stride >= b->width);
    CHECK(x >= 0 && y >= 0 && x < b->width && y < b->height);
    uint32_t actual = b->pixels[(size_t)y * (size_t)b->stride + (size_t)x];
    for (int shift = 0; shift < 32; shift += 8) {
        int delta = (int)((actual >> shift) & 255) - (int)((color >> shift) & 255);
        if (abs(delta) > tolerance) {
            fprintf(stderr, "render modules [%s] %s (%d,%d): %08lx, expected %08lx\n",
                phase, label, x, y, (unsigned long)actual, (unsigned long)color);
            exit(1);
        }
    }
}

static void quad(float *q, int x, int y, int w, int h, uint32_t tint) {
    const float data[10] = { (float)x, (float)y, (float)w, (float)h, 0, 0, 2, 2, 0, 0 };
    memcpy(q, data, sizeof(data));
    memcpy(q + 8, &tint, sizeof(tint)); /* 核心系统底层抽象与内存语义契约 */
}

static void test_images(void) {
    phase = "image";
    /* 模块核心语义抽象与接口调用契约 */
    const unsigned char tga[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28,
        0, 0, 255, 255, 0, 255, 0, 255,
        255, 0, 0, 255, 0xC0, 0x80, 0x40, 0x80
    };
    const char *key = "mem:render-tga";
    CHECK(image.load_bytes(key, (const char *)tga, (int32_t)sizeof(tga)) == 2);
    CHECK(image.width(key) == 2 && image.height(key) == 2);
    const zan_bitmap *b = image.get(key);
    CHECK(b && (b->serial & IMAGE_SERIAL_BIT));
    uint64_t serial = b->serial;
    expect("TGA red/top", b, 0, 0, RED, 0);
    expect("TGA green", b, 1, 0, GREEN, 0);
    expect("TGA blue/bottom", b, 0, 1, BLUE, 0);
    expect("TGA straight alpha", b, 1, 1, UINT32_C(0x804080C0), 0);
    image.evict(key);
    CHECK(!image.get(key) && image.width(key) == 0 && image.height(key) == 0);
    CHECK(image.load_mem(key, (const char *)tga, (int32_t)sizeof(tga)) == 2);
    b = image.get(key);
    CHECK(b && b->serial != serial && (b->serial & IMAGE_SERIAL_BIT));
    expect("TGA reload", b, 0, 1, BLUE, 0);
    image.evict(key);

    const char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"4\">"
        "<rect width=\"8\" height=\"4\" fill=\"#2468ac\"/></svg>";
    key = "mem:render-svg";
    for (int pass = 0; pass < 2; ++pass) {
        CHECK(image.load_svg(key, svg, (int32_t)(sizeof(svg) - 1), 16, 16) == 16);
        b = image.get(key);
        CHECK(b && b->width == 16 && b->height == 8 && (b->serial & IMAGE_SERIAL_BIT));
        if (pass) CHECK(b->serial != serial);
        serial = b->serial;
        expect("SVG contain/interior", b, 8, 4, UINT32_C(0xFF2468AC), 0);
        image.evict(key);
        CHECK(!image.get(key));
    }
}

static void test_order(int dst, int handle, int tolerance) {
    float q[10];
    gui.clear(dst, (int32_t)BLACK);
    gui.fill(dst, 0, 0, 12, 8, (int32_t)RED);
    quad(q, 2, 1, 8, 6, BLUE); game.batch(dst, handle, q, 1);
    gui.fill(dst, 4, 2, 4, 4, (int32_t)GREEN);
    /* 模块核心语义抽象与接口调用契约 */
    gui.snapshot(dst, 0, 0, 12, 8, 2);
    quad(q, 0, 0, 12, 8, WHITE); game.batch(dst, handle, q, 1);
    CHECK(gui.restore(dst, 0, 0, 12, 8, 2) == 1);
    quad(q, 6, 2, 3, 3, YELLOW); game.batch(dst, handle, q, 1);
    gui.fill(dst, 7, 3, 1, 1, (int32_t)MAGENTA);
    const zan_bitmap *b = gui.bitmap(dst);
    expect("2D before batch", b, 0, 0, RED, tolerance);
    expect("batch before fallback", b, 2, 2, BLUE, tolerance);
    expect("2D after batch", b, 4, 2, GREEN, tolerance);
    expect("batch after fallback", b, 6, 2, YELLOW, tolerance);
    expect("final 2D", b, 7, 3, MAGENTA, tolerance);
}

static void test_transient(int dst) {
    uint32_t pixels[8] = { RED, RED, MAGENTA, MAGENTA, RED, RED, MAGENTA, MAGENTA };
    zan_bitmap bitmap = { pixels, 2, 2, 4, 0 };
    float q[10];
    gui.clear(dst, (int32_t)BLACK);
    gui.blit(dst, &bitmap, 1, 1, 2, 2, 0, 0, 2, 2);
    pixels[0] = pixels[1] = pixels[4] = pixels[5] = BLUE;
    quad(q, 6, 1, 2, 2, WHITE); gui.batch(dst, &bitmap, q, 1);
    /* 模块核心语义抽象与接口调用契约 */
    memset(pixels, 0, sizeof(pixels));
    memset(&bitmap, 0, sizeof(bitmap));
    gui.fill(dst, 10, 1, 2, 2, (int32_t)GREEN);
    const zan_bitmap *b = gui.bitmap(dst);
    expect("first serial-zero view", b, 1, 1, RED, 1);
    expect("reused serial-zero view/stride", b, 6, 2, BLUE, 1);
    expect("2D after transient batch", b, 10, 1, GREEN, 1);
}

static void test_sprites(int gpu) {
    phase = gpu ? "GPU sprites" : "CPU sprites";
    const int tolerance = gpu ? 1 : 0; /* 底层系统交互与数据协议契约 */
    int src = gui.create(7, 5), dst = gui.create(16, 12);
    CHECK(src >= 0 && dst >= 0);
    gui.clear(src, (int32_t)MAGENTA);
    gui.clear(dst, (int32_t)BLACK);
    if (gpu) {
        zan_gpu_frame frame;
        CHECK(gui.begin(dst, &frame));
        CHECK(frame.api && frame.framebuffer);
        gui.end(dst);
    }
    gui.fill(src, 2, 1, 1, 1, (int32_t)RED);
    gui.fill(src, 3, 1, 1, 1, (int32_t)GREEN);
    gui.fill(src, 2, 2, 1, 1, (int32_t)BLUE);
    gui.fill(src, 3, 2, 1, 1, (int32_t)WHITE);
    int handle = game.bake("render-bake", src, 2, 1, 2, 2);
    CHECK(handle > 0 && game.handle("mem:render-bake") == handle);
    const zan_bitmap *b = image.get("mem:render-bake");
    CHECK(b && b->width == 2 && b->height == 2 && (b->serial & IMAGE_SERIAL_BIT));
    uint64_t serial = b->serial;
    gui.clear(src, (int32_t)BLACK); /* 底层系统交互与数据协议契约 */
    b = image.get("mem:render-bake");
    expect("bake owns copy", b, 0, 0, RED, tolerance);
    expect("slice crosses surface stride", b, 0, 1, BLUE, tolerance);
    float q[30];
    quad(q, 1, 1, 2, 2, WHITE); quad(q + 10, 6, 1, 2, 2, WHITE);
    game.batch(dst, handle, q, 2);
    b = gui.bitmap(dst);
    expect("batch first quad", b, 1, 1, RED, tolerance);
    expect("batch second quad/row", b, 6, 2, BLUE, tolerance);
    expect("white tint bits", b, 7, 2, WHITE, tolerance);

    gui.fill(src, 2, 1, 2, 2, (int32_t)WHITE);
    CHECK(game.bake("mem:render-bake", src, 2, 1, 2, 2) == handle);
    b = image.get("mem:render-bake");
    CHECK(b && b->serial != serial && (b->serial & IMAGE_SERIAL_BIT));
    CHECK(gui.destroy(src) == 0);
    gui.clear(dst, (int32_t)BLACK);
    gui.clip(dst, 3, 3, 2, 2);
    quad(q, 2, 2, 4, 4, UINT32_C(0x80FF0000));
    quad(q + 10, 3, 3, 1, 1, GREEN);
    quad(q + 20, 4, 4, 1, 1, UINT32_C(0x000000FF));
    game.batch(dst, handle, q, 3);
    gui.unclip(dst);
    quad(q, 8, 6, 2, 2, BLUE); game.batch(dst, handle, q, 1);
    b = gui.bitmap(dst);
    expect("same-key rebake/order", b, 3, 3, GREEN, tolerance);
    expect("tint alpha/transparent no-op", b, 4, 4, UINT32_C(0xFF800000), tolerance);
    expect("clipped out", b, 2, 3, BLACK, 0);
    expect("clip restored", b, 8, 6, BLUE, tolerance);
    CHECK(game.bake("render-clamp", dst, -1, -1, 3, 3) > 0);
    CHECK(image.width("mem:render-clamp") == 2 && image.height("mem:render-clamp") == 2);

    test_order(dst, handle, tolerance);
    if (gpu) test_transient(dst);
    gui.clear(dst, (int32_t)BLACK);
    gui.fill(dst, 0, 0, 2, 2, (int32_t)RED);
    gui.fill(dst, 2, 0, 2, 2, (int32_t)BLUE);
    gui.scaled(dst, dst, 2, 0, 8, 4, 0, 0, 4, 2);
    b = gui.bitmap(dst);
    /* 模块核心语义抽象与接口调用契约 */
    expect("scaled self-blit first half", b, 3, 1, RED, tolerance);
    expect("scaled self-blit saved source", b, 8, 1, BLUE, tolerance);
    expect("self-blit bounds", b, 10, 1, BLACK, 0);
    CHECK(gui.destroy(dst) == 0);
    image.evict("mem:render-bake"); image.evict("mem:render-clamp");
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s CORE GAME IMAGE\n", argv[0]); return 2; }
    module core = open_module(argv[1]);
    check_core_exports(core); /* 底层系统交互与数据协议契约 */
    module image_module = open_module(argv[3]); /* 核心系统底层抽象与内存语义契约 */
    module game_module = open_module(argv[2]);
    load_apis(core, game_module, image_module);
    test_images();
    CHECK(gui.backend(0) == 0 && strcmp(gui.backend_name(), "cpu") == 0);
    test_sprites(0);
    puts("render modules: image + CPU passed");
    if (gui.backend(1)) {
        CHECK(strcmp(gui.backend_name(), "gl") == 0);
        test_sprites(1);
        puts("render modules: GPU passed");
    } else {
        puts("render modules: GPU unavailable; image + CPU checks passed");
    }
    /* 模块核心语义抽象与接口调用契约 */
    gui.backend(0);
    close_module(game_module); close_module(image_module); close_module(core);
    return 0;
}
