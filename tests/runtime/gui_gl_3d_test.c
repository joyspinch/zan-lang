/* 模块核心语义抽象与接口调用契约 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
typedef HMODULE library;
#define OPEN(p) LoadLibraryA(p)
#define SYMBOL(l,n) GetProcAddress(l, n)
#else
#include <dlfcn.h>
typedef void *library;
#define OPEN(p) dlopen(p, RTLD_NOW | RTLD_GLOBAL)
#define SYMBOL(l,n) dlsym(l, n)
#endif
#define W 120
#define H 100
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "GL 3D: %s (line %d)\n", #c, __LINE__); exit(1); } } while (0)

static int32_t (*create)(int32_t,int32_t);
static int32_t (*destroy)(int32_t);
static int32_t (*backend)(int32_t);
static const char *(*backend_name)(void);
static const void *(*pixels)(int32_t);
static void (*clear_rect)(int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t);
static int32_t (*mesh_create)(int32_t,const float*,int32_t,const uint16_t*,int32_t);
static void (*push_clip)(int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*pop_clip)(int32_t);
static int32_t (*draw3d)(int32_t,int32_t,const float*,uint32_t,const char*);
#define LOAD(v,l,n) do { *(void **)(&(v)) = (void *)SYMBOL(l,n); CHECK(v); } while (0)

static uint32_t frame[W*H];
static void capture(int s) {
    const void *p = pixels(s);
    CHECK(p);
    memcpy(frame, p, sizeof(frame));
}
static int center_r(void) { uint32_t c = frame[(H/2)*W + W/2]; return (int)((c>>16)&255); }
static int center_g(void) { uint32_t c = frame[(H/2)*W + W/2]; return (int)((c>>8)&255); }
static uint32_t frame_hash(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < W*H; i++) { h ^= frame[i]; h *= 16777619u; }
    return h;
}

/* 模块核心语义抽象与接口调用契约 */
static float *cube_mvp(float rad, float dist, float r) {
    static float m[16];
    float c = (float)cos(rad), s = (float)sin(rad);
    /* 底层系统交互与数据协议契约 */
    float cy=(float)cos(0.4), sy=(float)sin(0.4);
    float R[16] = {
        c,        0,   -s,       0,
        sy*s,     cy,  sy*c,     0,
        cy*s,    -sy,  cy*c,     0,
        0,        0,    0,       1,
    };
    /* 模块核心语义抽象与接口调用契约 */
    float V[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-dist,1 };
    /* 核心系统底层抽象与内存语义契约 */
    float f = 1.0f / (float)tan(0.5235988);
    float P[16] = {
        f / ((float)W / (float)H), 0, 0, 0,
        0, f, 0, 0,
        0, 0, 10.0f/(1.0f-100.0f), (1.0f*100.0f)/(1.0f-100.0f),
        0, 0, -1, 0,
    };
    /* 核心系统底层抽象与内存语义契约 */
    float PV[16];
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += P[k*4+row] * V[col*4+k];
            PV[col*4+row] = v;
        }
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += PV[k*4+row] * R[col*4+k];
            m[col*4+row] = v;
        }
    (void)r;
    return m;
}

static float cube_verts[6*4*8];
static uint16_t cube_idx[36];
static void build_cube(void) {
    static const float faces[6][3] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},
    };
    static const float up[6][3] = {
        {0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1},
    };
    static const float right[6][3] = {
        {1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0},
    };
    int v = 0, idx = 0;
    for (int f = 0; f < 6; f++) {
        float u0 = -1, v0 = -1, u1 = 1, v1 = 1;
        uint16_t base = (uint16_t)v;
        for (int corner = 0; corner < 4; corner++) {
            float uu = (corner == 0 || corner == 3) ? u0 : u1;
            float vv = (corner < 2) ? v0 : v1;
            cube_verts[v*8+0] = right[f][0]*uu + up[f][0]*vv + faces[f][0];
            cube_verts[v*8+1] = right[f][1]*uu + up[f][1]*vv + faces[f][1];
            cube_verts[v*8+2] = right[f][2]*uu + up[f][2]*vv + faces[f][2];
            cube_verts[v*8+3] = faces[f][0];
            cube_verts[v*8+4] = faces[f][1];
            cube_verts[v*8+5] = faces[f][2];
            cube_verts[v*8+6] = (corner == 0 || corner == 3) ? 0 : 1;
            cube_verts[v*8+7] = (corner < 2) ? 1 : 0;
            v++;
        }
        cube_idx[idx++] = base;     cube_idx[idx++] = (uint16_t)(base+1); cube_idx[idx++] = (uint16_t)(base+2);
        cube_idx[idx++] = base;     cube_idx[idx++] = (uint16_t)(base+2); cube_idx[idx++] = (uint16_t)(base+3);
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 4);
    library core = OPEN(argv[1]);
    CHECK(core);
    library image = OPEN(argv[3]);
    CHECK(image);
    library game = OPEN(argv[2]);
    CHECK(game);
    LOAD(create, core, "zan_gui_create_surface");
    LOAD(destroy, core, "zan_gui_destroy_surface");
    LOAD(backend, core, "zan_gui_set_render_backend");
    LOAD(backend_name, core, "zan_gui_render_backend");
    LOAD(pixels, core, "zan_gui_get_pixels");
    LOAD(clear_rect, core, "zan_gui_clear_rect");
    LOAD(push_clip, core, "zan_gui_push_clip");
    LOAD(pop_clip, core, "zan_gui_pop_clip");
    LOAD(mesh_create, game, "zan_game_mesh_create");
    LOAD(draw3d, game, "zan_game_draw3d");

    build_cube();
    /* 模块核心语义抽象与接口调用契约 */
    backend(0);
    int s = create(W, H);
    CHECK(s >= 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    int mesh_cpu = mesh_create(s, cube_verts, 24, cube_idx, 36);
    (void)mesh_cpu;   /* 核心系统底层抽象与内存语义契约 */
    int cpu_ret = draw3d(s, 1, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL);
    capture(s);
    uint32_t cpu_hash = frame_hash();
    if (cpu_ret != 0) { fprintf(stderr, "GL 3D: CPU backend must report 0\n"); return 1; }

    /* 模块核心语义抽象与接口调用契约 */
    if (!backend(1) || strcmp(backend_name(), "gl")) {
        fprintf(stderr, "SKIP: real GL 3.3 unavailable\n");
        return 77;
    }
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    int mesh = mesh_create(s, cube_verts, 24, cube_idx, 36);
    CHECK(mesh > 0);
    int gl_ret = draw3d(s, mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL);
    CHECK(gl_ret == 1);
    capture(s);
    uint32_t h0 = frame_hash();
    CHECK(h0 != cpu_hash);
    /* 模块核心语义抽象与接口调用契约 */
    CHECK(center_r() > 0x50 && center_g() > 0x50);

    /* 底层系统交互与数据协议契约 */
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, mesh, cube_mvp(0.7f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    uint32_t h1 = frame_hash();
    CHECK(h0 != h1);

    /* 模块核心语义抽象与接口调用契约 */
    unsigned char checker[18 + 2*2*4] = {0};
    checker[2] = 2;             /* 核心系统底层抽象与内存语义契约 */
    checker[12] = 2; checker[14] = 2;   /* 2x2, little-endian */
    checker[16] = 32; checker[17] = 0x28; /* 32bpp, top-down */
    {
        const unsigned char px[2*2*4] = {
            255,0,0,255, 0,255,0,255,
            0,0,255,255, 255,255,0,255,
        };
        memcpy(checker + 18, px, sizeof(px));
    }
    int32_t (*img_mem)(const char*, const char*, int32_t);
    *(void **)(&img_mem) = (void *)SYMBOL(image, "zan_image_load_mem");
    CHECK(img_mem);
    CHECK(img_mem("mem:3dcheck", (const char *)checker, sizeof(checker)) > 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, mesh, cube_mvp(0.7f, 4.0f, 1.0f), 0xFFFFFFFF, "mem:3dcheck") == 1);
    capture(s);
    uint32_t h2 = frame_hash();
    CHECK(h1 != h2);

    /* 模块核心语义抽象与接口调用契约 */
    float quad_v[2*4*8] = {0};
    uint16_t quad_i[12];
    const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int q = 0; q < 2; q++) {
        uint16_t base = (uint16_t)(q * 4);
        for (int v = 0; v < 4; v++) {
            float *vertex = quad_v + (q * 4 + v) * 8;
            vertex[0] = corners[v][0];
            vertex[1] = corners[v][1];
            vertex[2] = q == 0 ? -0.5f : 0.5f;
            vertex[5] = 1.0f;
            vertex[6] = q == 0 ? 0.25f : 0.75f;
            vertex[7] = 0.5f;
        }
        const uint16_t indices[6] = {0,1,2,0,2,3};
        for (int i = 0; i < 6; i++) quad_i[q*6+i] = base + indices[i];
    }
    unsigned char depth_texture[18 + 8] = {0};
    depth_texture[2] = 2;
    depth_texture[12] = 2; depth_texture[14] = 1;
    depth_texture[16] = 32; depth_texture[17] = 0x28;
    const unsigned char colors[8] = {0,0,255,255, 255,0,0,255};
    memcpy(depth_texture + 18, colors, sizeof(colors));
    CHECK(img_mem("mem:depth", (const char *)depth_texture, sizeof(depth_texture)));
    int qmesh = mesh_create(s, quad_v, 8, quad_i, 12);
    CHECK(qmesh > 0);
    float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, qmesh, identity, 0xFFFFFFFF, "mem:depth") == 1);
    capture(s);
    uint32_t center = frame[(H/2)*W + W/2];
    CHECK(((center >> 16) & 255) > 150 && (center & 255) < 50);
    CHECK((center >> 24) == 255);

    /* 模块核心语义抽象与接口调用契约 */
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    push_clip(s, 0, 0, W/2, H);
    CHECK(draw3d(s, qmesh, identity, 0xFFFFFFFF, "mem:depth") == 1);
    pop_clip(s);
    clear_rect(s, W-4, 0, 4, H, 0xFF00FF00);
    capture(s);
    CHECK(frame[(H/2)*W + W/4] != 0xFF203040);
    CHECK(frame[(H/2)*W + 3*W/4] == 0xFF203040);
    CHECK(frame[(H/2)*W + W-2] == 0xFF00FF00);

    uint32_t before_switch = frame_hash();
    CHECK(backend(0) == 0);
    capture(s);
    CHECK(frame_hash() == before_switch);
    CHECK(draw3d(s, mesh, identity, 0xFFFFFFFF, NULL) == 0);
    CHECK(backend(1) == 1);
    CHECK(draw3d(s, mesh, identity, 0xFFFFFFFF, NULL) == 0);
    CHECK(draw3d(s, qmesh, identity, 0xFFFFFFFF, NULL) == 0);
    int new_mesh = mesh_create(s, quad_v, 8, quad_i, 12);
    CHECK(new_mesh > qmesh);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, new_mesh, identity, 0xFFFFFFFF, "mem:depth") == 1);
    capture(s);
    CHECK(frame_hash() != cpu_hash);

    /* 模块核心语义抽象与接口调用契约 */
    CHECK(destroy(s) == 0);
    s = create(W, H);
    CHECK(s >= 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, new_mesh, identity, 0xFFFFFFFF, "mem:depth") == 1);
    capture(s);
    CHECK(((frame[(H/2)*W + W/2] >> 16) & 255) > 150);

    destroy(s);
    fprintf(stderr, "GL 3D: OK\n");
    return 0;
}
