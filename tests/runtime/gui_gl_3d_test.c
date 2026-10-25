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
static int32_t (*mesh_create_ex)(int32_t,const float*,int32_t,const uint16_t*,int32_t,int32_t);
static void (*push_clip)(int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*pop_clip)(int32_t);
static int32_t (*draw3d)(int32_t,int32_t,const float*,uint32_t,const char*);
static int32_t (*set_lights)(int32_t,const float*,const float*,const float*);
static int32_t (*batch_begin)(int32_t);
static int32_t (*batch)(int32_t,int32_t,const char*,const float*,int32_t);
static int32_t (*batch_end)(int32_t);
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

static float *cube_mvp(float rad, float dist, float r) {
    static float m[16];
    float c = (float)cos(rad), s = (float)sin(rad);
    float cy=(float)cos(0.4), sy=(float)sin(0.4);
    float R[16] = {
        c,        0,   -s,       0,
        sy*s,     cy,  sy*c,     0,
        cy*s,    -sy,  cy*c,     0,
        0,        0,    0,       1,
    };
    float V[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-dist,1 };
    float f = 1.0f / (float)tan(0.5235988);
    float P[16] = {
        f / ((float)W / (float)H), 0, 0, 0,
        0, f, 0, 0,
        0, 0, 10.0f/(1.0f-100.0f), (1.0f*100.0f)/(1.0f-100.0f),
        0, 0, -1, 0,
    };
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

/* 批量实例的 MVP：同一透视矩阵 P（与 cube_mvp 一致，视空间恒等）乘
 * 平移 T(dx,dy,dz)，列主序输出。 */
static float *inst_mvp(float dx, float dy, float dz) {
    static float m[16];
    float f = 1.0f / (float)tan(0.5235988);
    float fa = f / ((float)W / (float)H);
    /* P 列主序四列 */
    float pc[4][4] = {
        { fa, 0, 0, 0 },
        { 0, f, 0, 0 },
        { 0, 0, 10.0f/(1.0f-100.0f), -1 },
        { 0, 0, (1.0f*100.0f)/(1.0f-100.0f), 0 },
    };
    float t[4] = { dx, dy, dz, 1 };
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float v;
            if (col < 3) {
                v = pc[col][row]; /* T 前三列 = 恒等 */
            } else {
                v = 0;
                for (int k = 0; k < 4; k++) v += pc[k][row] * t[k];
            }
            m[col*4+row] = v;
        }
    (void)dy;
    return m;
}

static void fill_instance(float *inst, const float *mvp,
                          float r, float g, float b, float a) {
    for (int i = 0; i < 16; i++) inst[i] = mvp[i];
    inst[16] = r; inst[17] = g; inst[18] = b; inst[19] = a;
}

/* 单四边形网格（z=0 平面，法线 +z，8 float/顶点，无顶点色）。 */
static int make_quad_mesh(int32_t s, float half) {
    float v[4*8] = {0};
    uint16_t idx[6] = {0,1,2,0,2,3};
    const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 4; i++) {
        v[i*8+0] = corners[i][0] * half;
        v[i*8+1] = corners[i][1] * half;
        v[i*8+5] = 1.0f;
    }
    return mesh_create(s, v, 4, idx, 6);
}

/* 顶点色四边形：左半红右半绿（x 从 -0.9 到 0.9 线性插值），
 * 12 float/顶点。返回 mesh_create_ex(flags=1) 的结果。 */
static int make_colored_quad(int32_t s) {
    float v[4*12] = {0};
    uint16_t idx[6] = {0,1,2,0,2,3};
    const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 4; i++) {
        float *p = v + i * 12;
        p[0] = corners[i][0] * 0.9f;
        p[1] = corners[i][1] * 0.5f;
        p[3] = 0; p[4] = 0; p[5] = 1.0f;
        p[6] = (corners[i][0] < 0) ? 0.0f : 1.0f;
        p[7] = 0.5f;
        p[8] = (corners[i][0] < 0) ? 1.0f : 0.0f;
        p[9] = (corners[i][0] < 0) ? 0.0f : 1.0f;
        p[10] = 0.0f;
        p[11] = 1.0f;
    }
    return mesh_create_ex(s, v, 4, idx, 6, 1);
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
    LOAD(mesh_create_ex, game, "zan_game_mesh_create_ex");
    LOAD(draw3d, game, "zan_game_draw3d");
    LOAD(set_lights, game, "zan_game_set_lights3d");
    LOAD(batch_begin, game, "zan_game_draw3d_batch_begin");
    LOAD(batch, game, "zan_game_draw3d_batch");
    LOAD(batch_end, game, "zan_game_draw3d_batch_end");

    build_cube();
    backend(0);
    int s = create(W, H);
    CHECK(s >= 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    int mesh_cpu = mesh_create(s, cube_verts, 24, cube_idx, 36);
    (void)mesh_cpu;   
    int cpu_ret = draw3d(s, 1, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL);
    capture(s);
    uint32_t cpu_hash = frame_hash();
    if (cpu_ret != 0) { fprintf(stderr, "GL 3D: CPU backend must report 0\n"); return 1; }

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
    CHECK(center_r() > 0x50 && center_g() > 0x50);

    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, mesh, cube_mvp(0.7f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    uint32_t h1 = frame_hash();
    CHECK(h0 != h1);

    unsigned char checker[18 + 2*2*4] = {0};
    checker[2] = 2;             
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

    CHECK(destroy(s) == 0);
    s = create(W, H);
    CHECK(s >= 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, new_mesh, identity, 0xFFFFFFFF, "mem:depth") == 1);
    capture(s);
    CHECK(((frame[(H/2)*W + W/2] >> 16) & 255) > 150);

    /* ---- 顶点色 / 批量实例（新管线） ------------------------------ */

    /* CPU 兜底档：batch 三导出全部拒绝 */
    CHECK(backend(0) == 0);
    CHECK(batch_begin(s) == 0);
    CHECK(batch(s, 1, NULL, NULL, 1) == 0);
    CHECK(batch_end(s) == 0);
    CHECK(backend(1) == 1);

    /* 网格容量 > 24（原上限） */
    int cap_prev = 0;
    for (int i = 0; i < 30; i++) {
        int m = make_quad_mesh(s, 0.5f);
        CHECK(m > cap_prev);
        cap_prev = m;
    }

    /* 顶点色网格（无纹理）：左红右绿，中心黄色混合 */
    int cmesh = make_colored_quad(s);
    CHECK(cmesh > 0);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, cmesh, identity, 0xFFFFFFFF, NULL) == 1);
    capture(s);
    {
        uint32_t left = frame[(H/2)*W + W/4];
        uint32_t mid = frame[(H/2)*W + W/2];
        uint32_t right = frame[(H/2)*W + 3*W/4];
        CHECK(((left >> 16) & 255) > 150 && ((left >> 8) & 255) < 110);
        CHECK(((right >> 8) & 255) > 150 && ((right >> 16) & 255) < 110);
        CHECK(((mid >> 16) & 255) > 100 && ((mid >> 8) & 255) > 100 &&
              (mid & 255) < 100);
        CHECK((mid >> 24) == 255);
    }

    /* 批内深度解析：A(z=-3,红) 先提交，B(z=-4,绿) 后提交。两实例屏幕
     * 范围分别约 x∈[38,67] 与 x∈[55,76]，重叠 x∈[55,67] 必须是更近的
     * A——若深度不生效，后画的 B 会盖掉红。 */
    int sqmesh = make_quad_mesh(s, 0.5f);
    CHECK(sqmesh > 0);
    float inst[2 * 20];
    fill_instance(inst, inst_mvp(-0.25f, 0.0f, -3.0f), 1, 0, 0, 1);
    fill_instance(inst + 20, inst_mvp(0.25f, 0.0f, -4.0f), 0, 1, 0, 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(batch_begin(s) == 1);
    CHECK(batch(s, sqmesh, NULL, inst, 2) == 1);
    CHECK(batch_end(s) == 1);
    capture(s);
    {
        uint32_t overlap = frame[(H/2)*W + 60];
        uint32_t only_a = frame[(H/2)*W + 42];
        uint32_t only_b = frame[(H/2)*W + 72];
        CHECK(((overlap >> 16) & 255) > 140 && ((overlap >> 8) & 255) < 100);
        CHECK(((only_a >> 16) & 255) > 140 && ((only_a >> 8) & 255) < 100);
        CHECK(((only_b >> 8) & 255) > 140 && ((only_b >> 16) & 255) < 100);
    }

    /* 跨网格批：左侧红四边形（z=-2.5，屏幕约 x∈[22,57]）+ 居中立方体
     * （前脸 z=-3，约 x∈[38,82]），同一 begin/end。重叠区 x=45 必须是
     * 更近的四边形——立方体后提交，深度不生效就会变成白。 */
    int cube2 = mesh_create(s, cube_verts, 24, cube_idx, 36);
    CHECK(cube2 > 0);
    float inst2[20];
    fill_instance(inst2, inst_mvp(-0.6f, 0.0f, -2.5f), 1, 0, 0, 1);
    float cube_inst[20];
    fill_instance(cube_inst, cube_mvp(0.0f, 4.0f, 1.0f), 1, 1, 1, 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(batch_begin(s) == 1);
    CHECK(batch(s, sqmesh, NULL, inst2, 1) == 1);
    CHECK(batch(s, cube2, NULL, NULL, 0) == 0); /* count=0 拒绝 */
    CHECK(batch(s, cube2, NULL, cube_inst, 1) == 1);
    CHECK(batch_end(s) == 1);
    capture(s);
    {
        uint32_t mid = frame[(H/2)*W + 60];
        uint32_t overlap = frame[(H/2)*W + 45];
        uint32_t only_quad = frame[(H/2)*W + 25];
        CHECK(((mid >> 16) & 255) > 0x50 && ((mid >> 8) & 255) > 0x50);
        CHECK(((overlap >> 16) & 255) > 140 && ((overlap >> 8) & 255) < 100);
        CHECK(((only_quad >> 16) & 255) > 140 && ((only_quad >> 8) & 255) < 100);
    }

    /* 顶点色 × 实例化（第 4 个着色器变体）+ 每实例 tint：
     * 红 tint × 左红右绿顶点色 = 左红右黑（z=-3，屏幕约 x∈[34,86]）。 */
    float inst3[20];
    fill_instance(inst3, inst_mvp(0.0f, 0.0f, -3.0f), 1, 0, 0, 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(batch_begin(s) == 1);
    CHECK(batch(s, cmesh, NULL, inst3, 1) == 1);
    CHECK(batch_end(s) == 1);
    capture(s);
    {
        uint32_t left = frame[(H/2)*W + 40];
        uint32_t right = frame[(H/2)*W + 80];
        CHECK(((left >> 16) & 255) > 140 && ((left >> 8) & 255) < 60);
        CHECK(((right >> 16) & 255) < 60 && ((right >> 8) & 255) < 60);
    }

    /* 验证动态网格池扩容：突破原有 256 静态上限，连续创建 300 个网格 */
    int last_mesh = 0;
    for (int mi = 0; mi < 300; ++mi) {
        int m = mesh_create(s, cube_verts, 24, cube_idx, 36);
        CHECK(m > 0);
        last_mesh = m;
    }
    CHECK(last_mesh > 0);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);

    /* ---- 光照 uniform 通路（A489） --------------------------------
     * 之前全部断言都在缺省键光下取得（缺省 = 历史"写死的"固定光照）。
     * h0 是缺省光照 + cube_mvp(0.0f) 的画面基线。 */

    /* 缺省状态必须重现历史基线：uniform 缺省值与旧着色器常量逐位一致 */
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(frame_hash() == h0);

    /* 非法参数拒绝（不改变光照状态） */
    const float front[3] = { 0.0f, 0.0f, 1.0f };
    const float back[3] = { 0.0f, 0.0f, -1.0f };
    const float full[3] = { 1.0f, 1.0f, 1.0f };
    const float dark[3] = { 0.0f, 0.0f, 0.0f };
    CHECK(set_lights(5, NULL, NULL, NULL) == 0);
    CHECK(set_lights(-1, NULL, NULL, NULL) == 0);
    CHECK(set_lights(1, NULL, full, NULL) == 0);

    /* 纯环境光（count=0，ambient 保留 0.55）：前脸中心 ≈ 140 */
    CHECK(set_lights(0, NULL, NULL, NULL) == 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(center_r() >= 130 && center_r() <= 150);
    CHECK(frame_hash() != h0);

    /* 正面单灯满贡献（法线经 0.4 弧度俯仰后 dot≈0.92）：中心显著变亮 */
    CHECK(set_lights(1, front, full, dark) == 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(center_r() >= 240);
    uint32_t h_front = frame_hash();
    CHECK(h_front != h0);

    /* 背面单灯：前脸背光，只剩 0 贡献 → 近黑 */
    CHECK(set_lights(1, back, full, dark) == 1);
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(center_r() <= 20);
    CHECK(frame_hash() != h_front);

    /* 两盏灯累加（uLightCount=2 走数组循环）：介于背面与正面光之间 */
    {
        const float dirs[6] = { 0.0f, 0.0f, 1.0f, 0.4f, 0.8f, 0.6f };
        const float cols[6] = { 0.2f, 0.2f, 0.2f, 0.25f, 0.25f, 0.25f };
        CHECK(set_lights(2, dirs, cols, dark) == 1);
    }
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(center_r() >= 60 && center_r() <= 160);
    CHECK(frame_hash() != h_front);

    /* 批量实例通路吃同一份光照 uniform：正面光下白 tint 四边形全亮 */
    {
        float instl[20];
        fill_instance(instl, inst_mvp(0.0f, 0.0f, -3.0f), 1, 1, 1, 1);
        CHECK(set_lights(1, front, full, dark) == 1);
        clear_rect(s, 0, 0, W, H, 0xFF203040);
        CHECK(batch_begin(s) == 1);
        CHECK(batch(s, sqmesh, NULL, instl, 1) == 1);
        CHECK(batch_end(s) == 1);
        capture(s);
        CHECK(((frame[(H/2)*W + W/2] >> 16) & 255) >= 250);

        /* 批中途换光照（begin 与 batch 之间 set）：下一次提交即生效 */
        CHECK(batch_begin(s) == 1);
        CHECK(set_lights(1, back, full, dark) == 1);
        CHECK(batch(s, sqmesh, NULL, instl, 1) == 1);
        CHECK(batch_end(s) == 1);
        capture(s);
        CHECK(((frame[(H/2)*W + W/2] >> 16) & 255) <= 10);
    }

    /* 恢复缺省参数（与历史常量相同的三元组）→ 画面逐位回到基线 h0 */
    {
        const float def_dir[3] = { 0.4f, 0.8f, 0.6f };
        const float def_col[3] = { 0.45f, 0.45f, 0.45f };
        const float def_amb[3] = { 0.55f, 0.55f, 0.55f };
        CHECK(set_lights(1, def_dir, def_col, def_amb) == 1);
    }
    clear_rect(s, 0, 0, W, H, 0xFF203040);
    CHECK(draw3d(s, last_mesh, cube_mvp(0.0f, 4.0f, 1.0f), 0xFFFFFFFF, NULL) == 1);
    capture(s);
    CHECK(frame_hash() == h0);

    destroy(s);
    fprintf(stderr, "GL 3D: OK\n");
    return 0;
}
