/* gui_gl_cad.c -- 跨平台 2D/3D CAD GPU 渲染引擎（GL 3.3 Core / GLES 3.0 / WebGL 2.0 + CPU 回退） */

#define ZCAD_MAX_LAYERS2D 256
#define ZCAD_MAX_MESHES3D 512
#define ZCAD_MAX_CLIP_PLANES 6

typedef struct {
    int used;
    uint32_t epoch;
    double ox, oy;
    zgl_uint line_vao, line_vbo;
    i32 line_count;
    zgl_uint arc_vao, arc_vbo;
    i32 arc_count;
    zgl_uint fill_vao, fill_vbo, fill_ebo;
    i32 fill_vcount, fill_icount;
    /* 仅在无 GPU（CPU 回退模式）下保留精简主机副本 */
    float *cpu_lines;
    i32 cpu_line_count;
    float *cpu_arcs;
    i32 cpu_arc_count;
} zcad_layer2d_t;

typedef struct {
    int used;
    uint32_t epoch;
    zgl_uint face_vao, face_vbo, face_ebo;
    i32 face_vcount, face_icount;
    zgl_uint edge_vao, edge_vbo, edge_ebo;
    i32 edge_vcount, edge_icount;
} zcad_mesh3d_t;

static struct {
    int ready;
    uint32_t epoch;
    zgl_int max_samples;
    zgl_uint empty_vao;

    /* 6 套 CAD 着色器及 uniform 缓存 */
    zgl_uint prog_grid;
    zgl_int u_grid_vp, u_grid_clip, u_grid_param, u_grid_origin;
    zgl_int u_grid_bg, u_grid_minor, u_grid_major, u_grid_ax, u_grid_ay;

    zgl_uint prog_line2d;
    zgl_int u_line_vp, u_line_rte, u_line_cam, u_line_clip, u_line_pick;

    zgl_uint prog_arc2d;
    zgl_int u_arc_vp, u_arc_rte, u_arc_cam, u_arc_clip, u_arc_pick;

    zgl_uint prog_fill2d;
    zgl_int u_fill_vp, u_fill_rte, u_fill_cam, u_fill_clip, u_fill_pick;

    zgl_uint prog_solid3d;
    zgl_int u_s3d_mvp, u_s3d_model, u_s3d_tint, u_s3d_clips, u_s3d_clipn;
    zgl_int u_s3d_flags, u_s3d_pick, u_s3d_id;

    zgl_uint prog_edge3d;
    zgl_int u_e3d_mvp, u_e3d_model, u_e3d_col, u_e3d_clips, u_e3d_clipn;
    zgl_int u_e3d_flags, u_e3d_pick, u_e3d_id;

    /* 3D 视口离屏渲染缓冲（4x MSAA + Depth24Stencil8，自动降级 1x） */
    zgl_uint fbo3d, rb3d_col, rb3d_ds;
    int fbo3d_w, fbo3d_h, fbo3d_samples;
    int in_3d;
    int vp3d_sid, vp3d_x, vp3d_y, vp3d_w, vp3d_h;
    float vp3d_mat[16];
    float clip_planes[ZCAD_MAX_CLIP_PLANES * 4];
    int clip_count;

    /* 硬件 Object-ID 拾取缓冲（RGBA8 24-bit ID + Depth24，Scissor 局部光栅化） */
    zgl_uint pick_fbo, pick_col_rb, pick_dep_rb;
    int pick_w, pick_h;
    int in_pick;
    int pick_sid, pick_px, pick_py, pick_radius;
    int pick_sx, pick_sy, pick_sw, pick_sh;
    int pick_3d_active;
    float pick_vp_mat[16];
} g_cad;

static zcad_layer2d_t g_cad_layers[ZCAD_MAX_LAYERS2D];
static zcad_mesh3d_t g_cad_meshes[ZCAD_MAX_MESHES3D];

static void zcad_argb_to_f4(u32 argb, float out[4]) {
    out[0] = (float)((argb >> 16) & 255u) / 255.0f;
    out[1] = (float)((argb >> 8) & 255u) / 255.0f;
    out[2] = (float)(argb & 255u) / 255.0f;
    out[3] = (float)((argb >> 24) & 255u) / 255.0f;
}

static void zcad_mat4_mul(float out[16], const float a[16], const float b[16]) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
}

static void zcad_layer_drop_gl(zcad_layer2d_t *L) {
    if (L->line_vao) gl.DeleteVertexArrays(1, &L->line_vao);
    if (L->line_vbo) gl.DeleteBuffers(1, &L->line_vbo);
    if (L->arc_vao) gl.DeleteVertexArrays(1, &L->arc_vao);
    if (L->arc_vbo) gl.DeleteBuffers(1, &L->arc_vbo);
    if (L->fill_vao) gl.DeleteVertexArrays(1, &L->fill_vao);
    if (L->fill_vbo) gl.DeleteBuffers(1, &L->fill_vbo);
    if (L->fill_ebo) gl.DeleteBuffers(1, &L->fill_ebo);
    L->line_vao = L->line_vbo = 0;
    L->arc_vao = L->arc_vbo = 0;
    L->fill_vao = L->fill_vbo = L->fill_ebo = 0;
    L->line_count = L->arc_count = L->fill_vcount = L->fill_icount = 0;
}

static void zcad_mesh_drop_gl(zcad_mesh3d_t *M) {
    if (M->face_vao) gl.DeleteVertexArrays(1, &M->face_vao);
    if (M->face_vbo) gl.DeleteBuffers(1, &M->face_vbo);
    if (M->face_ebo) gl.DeleteBuffers(1, &M->face_ebo);
    if (M->edge_vao) gl.DeleteVertexArrays(1, &M->edge_vao);
    if (M->edge_vbo) gl.DeleteBuffers(1, &M->edge_vbo);
    if (M->edge_ebo) gl.DeleteBuffers(1, &M->edge_ebo);
    M->face_vao = M->face_vbo = M->face_ebo = 0;
    M->edge_vao = M->edge_vbo = M->edge_ebo = 0;
    M->face_vcount = M->face_icount = M->edge_vcount = M->edge_icount = 0;
}

static void zcad_drop_fbo3d(void) {
    if (g_cad.fbo3d) gl.DeleteFramebuffers(1, &g_cad.fbo3d);
    if (g_cad.rb3d_col) gl.DeleteRenderbuffers(1, &g_cad.rb3d_col);
    if (g_cad.rb3d_ds) gl.DeleteRenderbuffers(1, &g_cad.rb3d_ds);
    g_cad.fbo3d = g_cad.rb3d_col = g_cad.rb3d_ds = 0;
    g_cad.fbo3d_w = g_cad.fbo3d_h = g_cad.fbo3d_samples = 0;
}

static void zcad_drop_pick_fbo(void) {
    if (g_cad.pick_fbo) gl.DeleteFramebuffers(1, &g_cad.pick_fbo);
    if (g_cad.pick_col_rb) gl.DeleteRenderbuffers(1, &g_cad.pick_col_rb);
    if (g_cad.pick_dep_rb) gl.DeleteRenderbuffers(1, &g_cad.pick_dep_rb);
    g_cad.pick_fbo = g_cad.pick_col_rb = g_cad.pick_dep_rb = 0;
    g_cad.pick_w = g_cad.pick_h = 0;
}

static void zcad_drop_all(void) {
    if (g_gl_state > 0) {
        for (int i = 0; i < ZCAD_MAX_LAYERS2D; i++) {
            if (g_cad_layers[i].used) zcad_layer_drop_gl(&g_cad_layers[i]);
        }
        for (int i = 0; i < ZCAD_MAX_MESHES3D; i++) {
            if (g_cad_meshes[i].used) zcad_mesh_drop_gl(&g_cad_meshes[i]);
        }
        zcad_drop_fbo3d();
        zcad_drop_pick_fbo();
        if (g_cad.empty_vao) gl.DeleteVertexArrays(1, &g_cad.empty_vao);
        if (g_cad.prog_grid) gl.DeleteProgram(g_cad.prog_grid);
        if (g_cad.prog_line2d) gl.DeleteProgram(g_cad.prog_line2d);
        if (g_cad.prog_arc2d) gl.DeleteProgram(g_cad.prog_arc2d);
        if (g_cad.prog_fill2d) gl.DeleteProgram(g_cad.prog_fill2d);
        if (g_cad.prog_solid3d) gl.DeleteProgram(g_cad.prog_solid3d);
        if (g_cad.prog_edge3d) gl.DeleteProgram(g_cad.prog_edge3d);
    }
    memset(&g_cad, 0, sizeof(g_cad));
}

static int zcad_ensure_init(void) {
    if (g_gl_state <= 0) return 0;
    if (!zan_gl_ctx_make_current()) return 0;
    if (g_cad.ready && g_cad.epoch == g_gpu_epoch) return 1;
    if (g_cad.ready) zcad_drop_all();

    const char *vs_head = g_zgl.is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";
    const char *fs_head = g_zgl.is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";

    g_cad.prog_grid = zgl_link_parts(vs_head, ZCAD_VS_GRID, fs_head, ZCAD_FS_GRID);
    g_cad.prog_line2d = zgl_link_parts(vs_head, ZCAD_VS_LINE2D, fs_head, ZCAD_FS_LINE2D);
    g_cad.prog_arc2d = zgl_link_parts(vs_head, ZCAD_VS_ARC2D, fs_head, ZCAD_FS_ARC2D);
    g_cad.prog_fill2d = zgl_link_parts(vs_head, ZCAD_VS_FILL2D, fs_head, ZCAD_FS_FILL2D);
    g_cad.prog_solid3d = zgl_link_parts(vs_head, ZCAD_VS_SOLID3D, fs_head, ZCAD_FS_SOLID3D);
    g_cad.prog_edge3d = zgl_link_parts(vs_head, ZCAD_VS_EDGE3D, fs_head, ZCAD_FS_EDGE3D);

    if (!g_cad.prog_grid || !g_cad.prog_line2d || !g_cad.prog_arc2d ||
        !g_cad.prog_fill2d || !g_cad.prog_solid3d || !g_cad.prog_edge3d) {
        zcad_drop_all();
        return 0;
    }

    gl.GenVertexArrays(1, &g_cad.empty_vao);

    g_cad.u_grid_vp = gl.GetUniformLocation(g_cad.prog_grid, "uViewport");
    g_cad.u_grid_clip = gl.GetUniformLocation(g_cad.prog_grid, "uClip");
    g_cad.u_grid_param = gl.GetUniformLocation(g_cad.prog_grid, "uGridParam");
    g_cad.u_grid_origin = gl.GetUniformLocation(g_cad.prog_grid, "uGridOrigin");
    g_cad.u_grid_bg = gl.GetUniformLocation(g_cad.prog_grid, "uBgColor");
    g_cad.u_grid_minor = gl.GetUniformLocation(g_cad.prog_grid, "uMinorColor");
    g_cad.u_grid_major = gl.GetUniformLocation(g_cad.prog_grid, "uMajorColor");
    g_cad.u_grid_ax = gl.GetUniformLocation(g_cad.prog_grid, "uAxisXColor");
    g_cad.u_grid_ay = gl.GetUniformLocation(g_cad.prog_grid, "uAxisYColor");

    g_cad.u_line_vp = gl.GetUniformLocation(g_cad.prog_line2d, "uViewport");
    g_cad.u_line_rte = gl.GetUniformLocation(g_cad.prog_line2d, "uRteOffset");
    g_cad.u_line_cam = gl.GetUniformLocation(g_cad.prog_line2d, "uCam");
    g_cad.u_line_clip = gl.GetUniformLocation(g_cad.prog_line2d, "uClip");
    g_cad.u_line_pick = gl.GetUniformLocation(g_cad.prog_line2d, "uPickMode");

    g_cad.u_arc_vp = gl.GetUniformLocation(g_cad.prog_arc2d, "uViewport");
    g_cad.u_arc_rte = gl.GetUniformLocation(g_cad.prog_arc2d, "uRteOffset");
    g_cad.u_arc_cam = gl.GetUniformLocation(g_cad.prog_arc2d, "uCam");
    g_cad.u_arc_clip = gl.GetUniformLocation(g_cad.prog_arc2d, "uClip");
    g_cad.u_arc_pick = gl.GetUniformLocation(g_cad.prog_arc2d, "uPickMode");

    g_cad.u_fill_vp = gl.GetUniformLocation(g_cad.prog_fill2d, "uViewport");
    g_cad.u_fill_rte = gl.GetUniformLocation(g_cad.prog_fill2d, "uRteOffset");
    g_cad.u_fill_cam = gl.GetUniformLocation(g_cad.prog_fill2d, "uCam");
    g_cad.u_fill_clip = gl.GetUniformLocation(g_cad.prog_fill2d, "uClip");
    g_cad.u_fill_pick = gl.GetUniformLocation(g_cad.prog_fill2d, "uPickMode");

    g_cad.u_s3d_mvp = gl.GetUniformLocation(g_cad.prog_solid3d, "uMvp");
    g_cad.u_s3d_model = gl.GetUniformLocation(g_cad.prog_solid3d, "uModel");
    g_cad.u_s3d_tint = gl.GetUniformLocation(g_cad.prog_solid3d, "uTint");
    g_cad.u_s3d_clips = gl.GetUniformLocation(g_cad.prog_solid3d, "uClipPlanes");
    g_cad.u_s3d_clipn = gl.GetUniformLocation(g_cad.prog_solid3d, "uClipCount");
    g_cad.u_s3d_flags = gl.GetUniformLocation(g_cad.prog_solid3d, "uFlags");
    g_cad.u_s3d_pick = gl.GetUniformLocation(g_cad.prog_solid3d, "uPickMode");
    g_cad.u_s3d_id = gl.GetUniformLocation(g_cad.prog_solid3d, "uEntityId");

    g_cad.u_e3d_mvp = gl.GetUniformLocation(g_cad.prog_edge3d, "uMvp");
    g_cad.u_e3d_model = gl.GetUniformLocation(g_cad.prog_edge3d, "uModel");
    g_cad.u_e3d_col = gl.GetUniformLocation(g_cad.prog_edge3d, "uEdgeColor");
    g_cad.u_e3d_clips = gl.GetUniformLocation(g_cad.prog_edge3d, "uClipPlanes");
    g_cad.u_e3d_clipn = gl.GetUniformLocation(g_cad.prog_edge3d, "uClipCount");
    g_cad.u_e3d_flags = gl.GetUniformLocation(g_cad.prog_edge3d, "uFlags");
    g_cad.u_e3d_pick = gl.GetUniformLocation(g_cad.prog_edge3d, "uPickMode");
    g_cad.u_e3d_id = gl.GetUniformLocation(g_cad.prog_edge3d, "uEntityId");

    g_cad.max_samples = 1;
    if (gl.GetIntegerv && gl.RenderbufferStorageMultisample) {
        zgl_int ms = 1;
        gl.GetIntegerv(ZGL_MAX_SAMPLES, &ms);
        if (ms > 1) g_cad.max_samples = ms;
    }

    g_cad.epoch = g_gpu_epoch;
    g_cad.ready = 1;
    return 1;
}

static zcad_layer2d_t *zcad_get_layer(i32 id) {
    if (id < 1 || id > ZCAD_MAX_LAYERS2D) return NULL;
    zcad_layer2d_t *L = &g_cad_layers[id - 1];
    return L->used ? L : NULL;
}

static zcad_mesh3d_t *zcad_get_mesh(i32 id) {
    if (id < 1 || id > ZCAD_MAX_MESHES3D) return NULL;
    zcad_mesh3d_t *M = &g_cad_meshes[id - 1];
    return M->used ? M : NULL;
}

static zan_surface_t *zcad_get_surface(i32 sid) {
    if (sid < 0 || sid >= g_surface_count) return NULL;
    return g_surfaces[sid];
}

static int zcad_surface_is_gl(zan_surface_t *s) {
    return s && s->be == &zan_gl_backend && g_gl_state > 0;
}

/* --- 能力查询 --- */

EXPORT i32 zan_gui_cad_is_gpu(i32 surface_id) {
    zan_surface_t *s = zcad_get_surface(surface_id);
    return zcad_surface_is_gl(s) ? 1 : 0;
}

EXPORT i32 zan_gui_cad_max_samples(void) {
    if (!zcad_ensure_init()) return 1;
    return (i32)g_cad.max_samples;
}

/* --- 2D 保留式 CAD 图层管理 --- */

EXPORT i32 zan_gui_cad_layer2d_create(void) {
    for (int i = 0; i < ZCAD_MAX_LAYERS2D; i++) {
        if (!g_cad_layers[i].used) {
            memset(&g_cad_layers[i], 0, sizeof(zcad_layer2d_t));
            g_cad_layers[i].used = 1;
            g_cad_layers[i].epoch = g_gpu_epoch;
            return i + 1;
        }
    }
    return 0;
}

EXPORT void zan_gui_cad_layer2d_destroy(i32 layer_id) {
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L) return;
    if (g_gl_state > 0 && zan_gl_ctx_make_current()) {
        zcad_layer_drop_gl(L);
    }
    free(L->cpu_lines);
    free(L->cpu_arcs);
    memset(L, 0, sizeof(*L));
}

EXPORT void zan_gui_cad_layer2d_set_origin(i32 layer_id, double ox, double oy) {
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L) return;
    L->ox = ox;
    L->oy = oy;
}

EXPORT i32 zan_gui_cad_layer2d_upload_lines(i32 layer_id, const float *data, i32 count) {
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L || count < 0 || (count > 0 && !data)) return 0;

    if (g_backend != &zan_gl_backend || !zcad_ensure_init()) {
        free(L->cpu_lines);
        L->cpu_lines = NULL;
        L->cpu_line_count = 0;
        if (count > 0) {
            size_t bytes = (size_t)count * 12u * sizeof(float);
            L->cpu_lines = (float *)malloc(bytes);
            if (!L->cpu_lines) return 0;
            memcpy(L->cpu_lines, data, bytes);
            L->cpu_line_count = count;
        }
        return 1;
    }

    if (L->epoch != g_gpu_epoch) {
        L->line_vao = L->line_vbo = L->arc_vao = L->arc_vbo = 0;
        L->fill_vao = L->fill_vbo = L->fill_ebo = 0;
        L->epoch = g_gpu_epoch;
    }
    if (!L->line_vao) {
        gl.GenVertexArrays(1, &L->line_vao);
        gl.GenBuffers(1, &L->line_vbo);
        gl.BindVertexArray(L->line_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->line_vbo);
        zgl_sizei stride = (zgl_sizei)(12 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)0);
        gl.VertexAttribDivisor(0, 1);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(4 * sizeof(float)));
        gl.VertexAttribDivisor(1, 1);
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(8 * sizeof(float)));
        gl.VertexAttribDivisor(2, 1);
    } else {
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->line_vbo);
    }
    gl.BufferData(ZGL_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)count * 12u * sizeof(float)),
                  count > 0 ? data : NULL, ZGL_DYNAMIC_DRAW);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    L->line_count = count;
    return 1;
}

EXPORT i32 zan_gui_cad_layer2d_upload_arcs(i32 layer_id, const float *data, i32 count) {
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L || count < 0 || (count > 0 && !data)) return 0;

    if (g_backend != &zan_gl_backend || !zcad_ensure_init()) {
        free(L->cpu_arcs);
        L->cpu_arcs = NULL;
        L->cpu_arc_count = 0;
        if (count > 0) {
            size_t bytes = (size_t)count * 14u * sizeof(float);
            L->cpu_arcs = (float *)malloc(bytes);
            if (!L->cpu_arcs) return 0;
            memcpy(L->cpu_arcs, data, bytes);
            L->cpu_arc_count = count;
        }
        return 1;
    }

    if (L->epoch != g_gpu_epoch) {
        L->line_vao = L->line_vbo = L->arc_vao = L->arc_vbo = 0;
        L->fill_vao = L->fill_vbo = L->fill_ebo = 0;
        L->epoch = g_gpu_epoch;
    }
    if (!L->arc_vao) {
        gl.GenVertexArrays(1, &L->arc_vao);
        gl.GenBuffers(1, &L->arc_vbo);
        gl.BindVertexArray(L->arc_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->arc_vbo);
        zgl_sizei stride = (zgl_sizei)(14 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)0);
        gl.VertexAttribDivisor(0, 1);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(4 * sizeof(float)));
        gl.VertexAttribDivisor(1, 1);
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(8 * sizeof(float)));
        gl.VertexAttribDivisor(2, 1);
        gl.EnableVertexAttribArray(3);
        gl.VertexAttribPointer(3, 2, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(12 * sizeof(float)));
        gl.VertexAttribDivisor(3, 1);
    } else {
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->arc_vbo);
    }
    gl.BufferData(ZGL_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)count * 14u * sizeof(float)),
                  count > 0 ? data : NULL, ZGL_DYNAMIC_DRAW);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    L->arc_count = count;
    return 1;
}

EXPORT i32 zan_gui_cad_layer2d_upload_fills(i32 layer_id, const float *verts, i32 vcount,
                                            const u32 *indices, i32 icount) {
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L || vcount < 0 || icount < 0) return 0;
    if ((vcount > 0 && !verts) || (icount > 0 && !indices)) return 0;
    if (!zcad_ensure_init()) return 1;

    if (L->epoch != g_gpu_epoch) {
        L->line_vao = L->line_vbo = L->arc_vao = L->arc_vbo = 0;
        L->fill_vao = L->fill_vbo = L->fill_ebo = 0;
        L->epoch = g_gpu_epoch;
    }
    if (!L->fill_vao) {
        gl.GenVertexArrays(1, &L->fill_vao);
        gl.GenBuffers(1, &L->fill_vbo);
        gl.GenBuffers(1, &L->fill_ebo);
        gl.BindVertexArray(L->fill_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->fill_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, L->fill_ebo);
        zgl_sizei stride = (zgl_sizei)(8 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 2, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)0);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(2 * sizeof(float)));
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 2, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(6 * sizeof(float)));
    } else {
        gl.BindVertexArray(L->fill_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, L->fill_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, L->fill_ebo);
    }
    gl.BufferData(ZGL_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)vcount * 8u * sizeof(float)),
                  vcount > 0 ? verts : NULL, ZGL_DYNAMIC_DRAW);
    gl.BufferData(ZGL_ELEMENT_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)icount * sizeof(u32)),
                  icount > 0 ? indices : NULL, ZGL_DYNAMIC_DRAW);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    L->fill_vcount = vcount;
    L->fill_icount = icount;
    return 1;
}

static void zcad_draw_layer2d_gl(zcad_layer2d_t *L, int vp_w, int vp_h,
                                 float clip_x0, float clip_y0, float clip_x1, float clip_y1,
                                 double cam_x, double cam_y, double zoom, double rot_rad,
                                 int pick_mode) {
    if (!L || L->epoch != g_gpu_epoch) return;
    if (zoom <= 1e-12) return;
    float rte_x = (float)(L->ox - cam_x);
    float rte_y = (float)(L->oy - cam_y);
    float cam3[3] = { (float)zoom, (float)cos(rot_rad), (float)sin(rot_rad) };

    if (pick_mode) {
        gl.Disable(ZGL_BLEND);
    } else {
        gl.Enable(ZGL_BLEND);
        gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                             ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
    }
    gl.Disable(ZGL_DEPTH_TEST);

    if (L->fill_icount > 0 && L->fill_vao) {
        gl.UseProgram(g_cad.prog_fill2d);
        gl.Uniform2f(g_cad.u_fill_vp, (float)vp_w, (float)vp_h);
        gl.Uniform2f(g_cad.u_fill_rte, rte_x, rte_y);
        gl.Uniform3fv(g_cad.u_fill_cam, 1, cam3);
        gl.Uniform4f(g_cad.u_fill_clip, clip_x0, clip_y0, clip_x1, clip_y1);
        gl.Uniform1i(g_cad.u_fill_pick, pick_mode);
        gl.BindVertexArray(L->fill_vao);
        gl.DrawElements(ZGL_TRIANGLES, L->fill_icount, ZGL_UNSIGNED_INT, (const void *)0);
    }
    if (L->line_count > 0 && L->line_vao && gl.DrawArraysInstanced) {
        gl.UseProgram(g_cad.prog_line2d);
        gl.Uniform2f(g_cad.u_line_vp, (float)vp_w, (float)vp_h);
        gl.Uniform2f(g_cad.u_line_rte, rte_x, rte_y);
        gl.Uniform3fv(g_cad.u_line_cam, 1, cam3);
        gl.Uniform4f(g_cad.u_line_clip, clip_x0, clip_y0, clip_x1, clip_y1);
        gl.Uniform1i(g_cad.u_line_pick, pick_mode);
        gl.BindVertexArray(L->line_vao);
        gl.DrawArraysInstanced(ZGL_TRIANGLES, 0, 6, L->line_count);
    }
    if (L->arc_count > 0 && L->arc_vao && gl.DrawArraysInstanced) {
        gl.UseProgram(g_cad.prog_arc2d);
        gl.Uniform2f(g_cad.u_arc_vp, (float)vp_w, (float)vp_h);
        gl.Uniform2f(g_cad.u_arc_rte, rte_x, rte_y);
        gl.Uniform3fv(g_cad.u_arc_cam, 1, cam3);
        gl.Uniform4f(g_cad.u_arc_clip, clip_x0, clip_y0, clip_x1, clip_y1);
        gl.Uniform1i(g_cad.u_arc_pick, pick_mode);
        gl.BindVertexArray(L->arc_vao);
        gl.DrawArraysInstanced(ZGL_TRIANGLES, 0, 6, L->arc_count);
    }
}

static u32 zcad_f4_to_argb(float r, float g, float b, float a) {
    u32 R = (u32)(r < 0.0f ? 0.0f : (r > 1.0f ? 255.0f : r * 255.0f + 0.5f));
    u32 G = (u32)(g < 0.0f ? 0.0f : (g > 1.0f ? 255.0f : g * 255.0f + 0.5f));
    u32 B = (u32)(b < 0.0f ? 0.0f : (b > 1.0f ? 255.0f : b * 255.0f + 0.5f));
    u32 A = (u32)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f + 0.5f));
    return (A << 24) | (R << 16) | (G << 8) | B;
}

static void zcad_draw_layer2d_cpu(zan_surface_t *s, zcad_layer2d_t *L,
                                  double cam_x, double cam_y, double zoom, double rot_rad) {
    double c = cos(rot_rad), sn = sin(rot_rad);
    double hw = (double)s->width * 0.5, hh = (double)s->height * 0.5;
    double ox = L->ox - cam_x, oy = L->oy - cam_y;

    for (i32 i = 0; i < L->cpu_line_count; i++) {
        const float *f = L->cpu_lines + (size_t)i * 12u;
        double x0 = (double)f[0] + ox, y0 = (double)f[1] + oy;
        double x1 = (double)f[2] + ox, y1 = (double)f[3] + oy;
        int sx0 = (int)(hw + (x0 * c - y0 * sn) * zoom + 0.5);
        int sy0 = (int)(hh - (x0 * sn + y0 * c) * zoom + 0.5);
        int sx1 = (int)(hw + (x1 * c - y1 * sn) * zoom + 0.5);
        int sy1 = (int)(hh - (x1 * sn + y1 * c) * zoom + 0.5);
        int flg = (int)(f[5] + 0.5f);
        int th = (int)(((flg & 1) ? ((double)f[4] * zoom) : (double)f[4]) + 0.5);
        if (th < 1) th = 1;
        u32 col = zcad_f4_to_argb(f[8], f[9], f[10], f[11]);
        zan_cpu_backend.draw_line(s, sx0, sy0, sx1, sy1, col, th);
    }
    for (i32 i = 0; i < L->cpu_arc_count; i++) {
        const float *f = L->cpu_arcs + (size_t)i * 14u;
        double x = (double)f[0] + ox, y = (double)f[1] + oy;
        int sx = (int)(hw + (x * c - y * sn) * zoom + 0.5);
        int sy = (int)(hh - (x * sn + y * c) * zoom + 0.5);
        int r = (int)((double)f[2] * zoom + 0.5);
        if (r < 1) continue;
        int flg = (int)(f[12] + 0.5f);
        u32 col = zcad_f4_to_argb(f[8], f[9], f[10], f[11]);
        if (flg & 8) {
            zan_cpu_backend.fill_circle(s, sx, sy, r, col);
        } else {
            int th = (int)(((flg & 1) ? ((double)f[7] * zoom) : (double)f[7]) + 0.5);
            if (th < 1) th = 1;
            zan_cpu_backend.draw_circle(s, sx, sy, r, col, th);
        }
    }
    s->painted = 1;
}

EXPORT void zan_gui_cad_layer2d_draw(i32 surface_id, i32 layer_id,
                                     double cam_x, double cam_y, double zoom, double rot_rad) {
    zan_surface_t *s = zcad_get_surface(surface_id);
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!s || !L || zoom <= 1e-12) return;

    if (!zcad_surface_is_gl(s) || !zcad_ensure_init()) {
        zcad_draw_layer2d_cpu(s, L, cam_x, cam_y, zoom, rot_rad);
        return;
    }
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    zgl_upload(s, t);

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    gl.Viewport(0, 0, t->w, t->h);
    gl.Disable(ZGL_SCISSOR_TEST);
    zcad_draw_layer2d_gl(L, t->w, t->h,
                         (float)s->clip_x0, (float)s->clip_y0,
                         (float)s->clip_x1, (float)s->clip_y1,
                         cam_x, cam_y, zoom, rot_rad, 0);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    zgl_target_invalidate_base(t);
    t->opaque_dst = 0;
    t->gpu_ahead = 1;
    s->painted = 1;
}

/* --- 2D 无限工程栅格 --- */

EXPORT void zan_gui_cad_draw_grid2d(i32 surface_id, double cam_x, double cam_y, double zoom,
                                    double minor_world_step, i32 major_every,
                                    u32 bg_argb, u32 minor_argb, u32 major_argb,
                                    u32 axis_x_argb, u32 axis_y_argb) {
    zan_surface_t *s = zcad_get_surface(surface_id);
    if (!s || zoom <= 1e-12) return;
    if (minor_world_step <= 1e-9) minor_world_step = 10.0;
    if (major_every < 1) major_every = 5;

    if (!zcad_surface_is_gl(s) || !zcad_ensure_init()) {
        int x0 = s->clip_x0, y0 = s->clip_y0;
        int w = s->clip_x1 - s->clip_x0, h = s->clip_y1 - s->clip_y0;
        if (w > 0 && h > 0) {
            zan_cpu_backend.fill_rect(s, x0, y0, w, h, bg_argb);
            int ox = (int)((double)s->width * 0.5 - cam_x * zoom + 0.5);
            int oy = (int)((double)s->height * 0.5 + cam_y * zoom + 0.5);
            zan_cpu_backend.draw_line(s, x0, oy, x0 + w, oy, axis_x_argb, 1);
            zan_cpu_backend.draw_line(s, ox, y0, ox, y0 + h, axis_y_argb, 1);
            s->painted = 1;
        }
        return;
    }

    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return;
    zgl_upload(s, t);

    double minor_px = minor_world_step * zoom;
    double major_world = minor_world_step * (double)major_every;
    double major_px = major_world * zoom;
    double mod_min_x = fmod(cam_x, minor_world_step) * zoom;
    double mod_min_y = fmod(cam_y, minor_world_step) * zoom;
    double mod_maj_x = fmod(cam_x, major_world) * zoom;
    double mod_maj_y = fmod(cam_y, major_world) * zoom;
    double orig_sx = (double)t->w * 0.5 - cam_x * zoom;
    double orig_sy = (double)t->h * 0.5 + cam_y * zoom;
    if (orig_sx < -1e6) orig_sx = -1e6; else if (orig_sx > 1e6) orig_sx = 1e6;
    if (orig_sy < -1e6) orig_sy = -1e6; else if (orig_sy > 1e6) orig_sy = 1e6;

    float c_bg[4], c_min[4], c_maj[4], c_ax[4], c_ay[4];
    zcad_argb_to_f4(bg_argb, c_bg);
    zcad_argb_to_f4(minor_argb, c_min);
    zcad_argb_to_f4(major_argb, c_maj);
    zcad_argb_to_f4(axis_x_argb, c_ax);
    zcad_argb_to_f4(axis_y_argb, c_ay);

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    gl.Viewport(0, 0, t->w, t->h);
    gl.Disable(ZGL_SCISSOR_TEST);
    gl.Disable(ZGL_DEPTH_TEST);
    gl.Enable(ZGL_BLEND);
    gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                         ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
    gl.UseProgram(g_cad.prog_grid);
    gl.Uniform2f(g_cad.u_grid_vp, (float)t->w, (float)t->h);
    gl.Uniform4f(g_cad.u_grid_clip, (float)s->clip_x0, (float)s->clip_y0,
                 (float)s->clip_x1, (float)s->clip_y1);
    gl.Uniform4f(g_cad.u_grid_param, (float)minor_px, (float)major_every,
                 (float)mod_min_x, (float)mod_min_y);
    gl.Uniform4f(g_cad.u_grid_origin, (float)orig_sx, (float)orig_sy,
                 (float)mod_maj_x, (float)mod_maj_y);
    gl.Uniform4f(g_cad.u_grid_bg, c_bg[0], c_bg[1], c_bg[2], c_bg[3]);
    gl.Uniform4f(g_cad.u_grid_minor, c_min[0], c_min[1], c_min[2], c_min[3]);
    gl.Uniform4f(g_cad.u_grid_major, c_maj[0], c_maj[1], c_maj[2], c_maj[3]);
    gl.Uniform4f(g_cad.u_grid_ax, c_ax[0], c_ax[1], c_ax[2], c_ax[3]);
    gl.Uniform4f(g_cad.u_grid_ay, c_ay[0], c_ay[1], c_ay[2], c_ay[3]);
    gl.BindVertexArray(g_cad.empty_vao);
    gl.DrawArrays(ZGL_TRIANGLES, 0, 6);
    gl.BindVertexArray(0);
    (void)major_px;
    zgl_invalidate_state();
    zgl_target_invalidate_base(t);
    t->opaque_dst = 0;
    t->gpu_ahead = 1;
    s->painted = 1;
}

/* --- 3D CAD B-Rep 实体 / 拓扑锐边网格 --- */

EXPORT i32 zan_gui_cad_mesh3d_create(void) {
    for (int i = 0; i < ZCAD_MAX_MESHES3D; i++) {
        if (!g_cad_meshes[i].used) {
            memset(&g_cad_meshes[i], 0, sizeof(zcad_mesh3d_t));
            g_cad_meshes[i].used = 1;
            g_cad_meshes[i].epoch = g_gpu_epoch;
            return i + 1;
        }
    }
    return 0;
}

EXPORT void zan_gui_cad_mesh3d_destroy(i32 mesh_id) {
    zcad_mesh3d_t *M = zcad_get_mesh(mesh_id);
    if (!M) return;
    if (g_gl_state > 0 && zan_gl_ctx_make_current()) {
        zcad_mesh_drop_gl(M);
    }
    memset(M, 0, sizeof(*M));
}

EXPORT i32 zan_gui_cad_mesh3d_upload_faces(i32 mesh_id, const float *verts, i32 vcount,
                                           const u32 *indices, i32 icount) {
    zcad_mesh3d_t *M = zcad_get_mesh(mesh_id);
    if (!M || vcount < 0 || icount < 0) return 0;
    if ((vcount > 0 && !verts) || (icount > 0 && !indices)) return 0;
    if (!zcad_ensure_init()) return 1;

    if (M->epoch != g_gpu_epoch) {
        M->face_vao = M->face_vbo = M->face_ebo = 0;
        M->edge_vao = M->edge_vbo = M->edge_ebo = 0;
        M->epoch = g_gpu_epoch;
    }
    if (!M->face_vao) {
        gl.GenVertexArrays(1, &M->face_vao);
        gl.GenBuffers(1, &M->face_vbo);
        gl.GenBuffers(1, &M->face_ebo);
        gl.BindVertexArray(M->face_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, M->face_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, M->face_ebo);
        zgl_sizei stride = (zgl_sizei)(10 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 3, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)0);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 3, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(3 * sizeof(float)));
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(6 * sizeof(float)));
    } else {
        gl.BindVertexArray(M->face_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, M->face_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, M->face_ebo);
    }
    gl.BufferData(ZGL_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)vcount * 10u * sizeof(float)),
                  vcount > 0 ? verts : NULL, ZGL_STATIC_DRAW);
    gl.BufferData(ZGL_ELEMENT_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)icount * sizeof(u32)),
                  icount > 0 ? indices : NULL, ZGL_STATIC_DRAW);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    M->face_vcount = vcount;
    M->face_icount = icount;
    return 1;
}

EXPORT i32 zan_gui_cad_mesh3d_upload_edges(i32 mesh_id, const float *verts, i32 vcount,
                                           const u32 *indices, i32 icount) {
    zcad_mesh3d_t *M = zcad_get_mesh(mesh_id);
    if (!M || vcount < 0 || icount < 0) return 0;
    if ((vcount > 0 && !verts) || (icount > 0 && !indices)) return 0;
    if (!zcad_ensure_init()) return 1;

    if (M->epoch != g_gpu_epoch) {
        M->face_vao = M->face_vbo = M->face_ebo = 0;
        M->edge_vao = M->edge_vbo = M->edge_ebo = 0;
        M->epoch = g_gpu_epoch;
    }
    if (!M->edge_vao) {
        gl.GenVertexArrays(1, &M->edge_vao);
        gl.GenBuffers(1, &M->edge_vbo);
        gl.GenBuffers(1, &M->edge_ebo);
        gl.BindVertexArray(M->edge_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, M->edge_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, M->edge_ebo);
        zgl_sizei stride = (zgl_sizei)(7 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 3, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)0);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 4, ZGL_FLOAT, ZGL_FALSE, stride, (const void *)(3 * sizeof(float)));
    } else {
        gl.BindVertexArray(M->edge_vao);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, M->edge_vbo);
        gl.BindBuffer(ZGL_ELEMENT_ARRAY_BUFFER, M->edge_ebo);
    }
    gl.BufferData(ZGL_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)vcount * 7u * sizeof(float)),
                  vcount > 0 ? verts : NULL, ZGL_STATIC_DRAW);
    gl.BufferData(ZGL_ELEMENT_ARRAY_BUFFER, (zgl_sizeiptr)((size_t)icount * sizeof(u32)),
                  icount > 0 ? indices : NULL, ZGL_STATIC_DRAW);
    gl.BindVertexArray(0);
    zgl_invalidate_state();
    M->edge_vcount = vcount;
    M->edge_icount = icount;
    return 1;
}

/* --- 3D 视口 MSAA + Depth24Stencil8 通道 --- */

static int zcad_ensure_fbo3d(int w, int h, int req_samples) {
    int samples = req_samples;
    if (samples < 1) samples = 1;
    if (samples > (int)g_cad.max_samples) samples = (int)g_cad.max_samples;
    if (!gl.RenderbufferStorageMultisample) samples = 1;
    if (g_cad.fbo3d && g_cad.fbo3d_w == w && g_cad.fbo3d_h == h &&
        g_cad.fbo3d_samples == samples) {
        return 1;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        int s = (attempt == 0) ? samples : 1;
        zcad_drop_fbo3d();
        gl.GenFramebuffers(1, &g_cad.fbo3d);
        gl.BindFramebuffer(ZGL_FRAMEBUFFER, g_cad.fbo3d);
        gl.GenRenderbuffers(1, &g_cad.rb3d_col);
        gl.BindRenderbuffer(ZGL_RENDERBUFFER, g_cad.rb3d_col);
        if (s > 1) {
            gl.RenderbufferStorageMultisample(ZGL_RENDERBUFFER, s, ZGL_RGBA8, w, h);
        } else {
            gl.RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_RGBA8, w, h);
        }
        gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                                   ZGL_RENDERBUFFER, g_cad.rb3d_col);

        gl.GenRenderbuffers(1, &g_cad.rb3d_ds);
        gl.BindRenderbuffer(ZGL_RENDERBUFFER, g_cad.rb3d_ds);
        if (s > 1) {
            gl.RenderbufferStorageMultisample(ZGL_RENDERBUFFER, s, ZGL_DEPTH24_STENCIL8, w, h);
            gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_DEPTH_STENCIL_ATTACHMENT,
                                       ZGL_RENDERBUFFER, g_cad.rb3d_ds);
        } else {
            gl.RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH24_STENCIL8, w, h);
            gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_DEPTH_STENCIL_ATTACHMENT,
                                       ZGL_RENDERBUFFER, g_cad.rb3d_ds);
        }
        if (gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) == ZGL_FRAMEBUFFER_COMPLETE) {
            g_cad.fbo3d_w = w;
            g_cad.fbo3d_h = h;
            g_cad.fbo3d_samples = s;
            return 1;
        }
        if (s == 1) break;
    }
    zcad_drop_fbo3d();
    return 0;
}

EXPORT i32 zan_gui_cad_3d_begin(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                u32 clear_argb, i32 msaa_samples,
                                const float *view_proj_16) {
    zan_surface_t *s = zcad_get_surface(surface_id);
    if (!s || w <= 0 || h <= 0 || !view_proj_16) return 0;

    if (!zcad_surface_is_gl(s) || !zcad_ensure_init()) {
        zan_cpu_backend.fill_rect(s, x, y, w, h, clear_argb);
        s->painted = 1;
        return 0;
    }
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!t) return 0;
    zgl_upload(s, t);

    if (!zcad_ensure_fbo3d(w, h, msaa_samples)) return 0;

    float bg[4];
    zcad_argb_to_f4(clear_argb, bg);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, g_cad.fbo3d);
    gl.Viewport(0, 0, w, h);
    gl.Disable(ZGL_SCISSOR_TEST);
    if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
    gl.Enable(ZGL_DEPTH_TEST);
    gl.DepthFunc(ZGL_LESS);
    gl.ClearColor(bg[0], bg[1], bg[2], bg[3]);
    if (gl.ClearStencil) gl.ClearStencil(0);
    gl.Clear(ZGL_COLOR_BUFFER_BIT | ZGL_DEPTH_BUFFER_BIT | ZGL_STENCIL_BUFFER_BIT);

    g_cad.in_3d = 1;
    g_cad.vp3d_sid = surface_id;
    g_cad.vp3d_x = x;
    g_cad.vp3d_y = y;
    g_cad.vp3d_w = w;
    g_cad.vp3d_h = h;
    memcpy(g_cad.vp3d_mat, view_proj_16, 16 * sizeof(float));
    g_cad.clip_count = 0;
    return 1;
}

EXPORT void zan_gui_cad_3d_set_clip_planes(const float *planes_4f, i32 count) {
    if (count < 0) count = 0;
    if (count > ZCAD_MAX_CLIP_PLANES) count = ZCAD_MAX_CLIP_PLANES;
    g_cad.clip_count = count;
    if (count > 0 && planes_4f) {
        memcpy(g_cad.clip_planes, planes_4f, (size_t)count * 4u * sizeof(float));
    }
}

static void zcad_draw_mesh3d_internal(zcad_mesh3d_t *M, const float *vp_mat,
                                      const float *model_16,
                                      u32 face_tint_argb, u32 edge_argb,
                                      i32 flags, i32 entity_id, int pick_mode) {
    if (!M || M->epoch != g_gpu_epoch) return;
    static const float kIdent[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    const float *mod = model_16 ? model_16 : kIdent;
    float mvp[16];
    zcad_mat4_mul(mvp, vp_mat, mod);

    int draw_faces = (flags & 1) != 0 || pick_mode;
    int draw_edges = (flags & 2) != 0 && !pick_mode;
    int xray = (flags & 8) != 0 && !pick_mode;
    int cull = (flags & 16) != 0;

    if (cull && gl.CullFace) {
        gl.Enable(ZGL_CULL_FACE);
        gl.CullFace(ZGL_BACK);
    } else {
        gl.Disable(ZGL_CULL_FACE);
    }

    if (draw_faces && M->face_icount > 0 && M->face_vao) {
        if (pick_mode) {
            gl.Disable(ZGL_BLEND);
            if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
        } else if (xray) {
            gl.Enable(ZGL_BLEND);
            gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                                 ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
            if (gl.DepthMask) gl.DepthMask(ZGL_FALSE);
        } else {
            gl.Disable(ZGL_BLEND);
            if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
        }
        if (draw_edges && gl.PolygonOffset) {
            gl.Enable(ZGL_POLYGON_OFFSET_FILL);
            gl.PolygonOffset(1.0f, 1.0f);
        } else {
            gl.Disable(ZGL_POLYGON_OFFSET_FILL);
        }
        float tint[4];
        zcad_argb_to_f4(face_tint_argb, tint);
        gl.UseProgram(g_cad.prog_solid3d);
        gl.UniformMatrix4fv(g_cad.u_s3d_mvp, 1, ZGL_FALSE, mvp);
        gl.UniformMatrix4fv(g_cad.u_s3d_model, 1, ZGL_FALSE, mod);
        gl.Uniform4f(g_cad.u_s3d_tint, tint[0], tint[1], tint[2], tint[3]);
        if (g_cad.clip_count > 0 && gl.Uniform4fv) {
            gl.Uniform4fv(g_cad.u_s3d_clips, g_cad.clip_count, g_cad.clip_planes);
        }
        gl.Uniform1i(g_cad.u_s3d_clipn, g_cad.clip_count);
        gl.Uniform1i(g_cad.u_s3d_flags, flags);
        gl.Uniform1i(g_cad.u_s3d_pick, pick_mode);
        gl.Uniform1i(g_cad.u_s3d_id, entity_id);
        gl.BindVertexArray(M->face_vao);
        gl.DrawElements(ZGL_TRIANGLES, M->face_icount, ZGL_UNSIGNED_INT, (const void *)0);
        gl.Disable(ZGL_POLYGON_OFFSET_FILL);
        if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
    }

    if (draw_edges && M->edge_vcount > 0 && M->edge_vao) {
        float ec[4];
        zcad_argb_to_f4(edge_argb, ec);
        gl.Enable(ZGL_BLEND);
        gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                             ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
        gl.UseProgram(g_cad.prog_edge3d);
        gl.UniformMatrix4fv(g_cad.u_e3d_mvp, 1, ZGL_FALSE, mvp);
        gl.UniformMatrix4fv(g_cad.u_e3d_model, 1, ZGL_FALSE, mod);
        gl.Uniform4f(g_cad.u_e3d_col, ec[0], ec[1], ec[2], ec[3]);
        if (g_cad.clip_count > 0 && gl.Uniform4fv) {
            gl.Uniform4fv(g_cad.u_e3d_clips, g_cad.clip_count, g_cad.clip_planes);
        }
        gl.Uniform1i(g_cad.u_e3d_clipn, g_cad.clip_count);
        gl.Uniform1i(g_cad.u_e3d_flags, flags);
        gl.Uniform1i(g_cad.u_e3d_pick, pick_mode);
        gl.Uniform1i(g_cad.u_e3d_id, entity_id);
        gl.BindVertexArray(M->edge_vao);
        if (M->edge_icount > 0) {
            gl.DrawElements(ZGL_LINES, M->edge_icount, ZGL_UNSIGNED_INT, (const void *)0);
        } else {
            gl.DrawArrays(ZGL_LINES, 0, M->edge_vcount);
        }
    }
}

EXPORT void zan_gui_cad_3d_draw_mesh(i32 mesh_id, const float *model_16,
                                     u32 face_tint_argb, u32 edge_argb,
                                     i32 flags, i32 entity_id) {
    if (!g_cad.in_3d) return;
    zcad_mesh3d_t *M = zcad_get_mesh(mesh_id);
    if (!M) return;
    zcad_draw_mesh3d_internal(M, g_cad.vp3d_mat, model_16,
                              face_tint_argb, edge_argb, flags, entity_id, 0);
}

EXPORT void zan_gui_cad_3d_end(i32 surface_id) {
    if (!g_cad.in_3d || g_cad.vp3d_sid != surface_id) return;
    g_cad.in_3d = 0;
    zan_surface_t *s = zcad_get_surface(surface_id);
    if (!s || !zcad_surface_is_gl(s)) return;
    zgl_target *t = zgl_target_of(s);
    if (!t) return;

    int dx0 = g_cad.vp3d_x;
    int dy0 = t->h - (g_cad.vp3d_y + g_cad.vp3d_h);
    int dx1 = dx0 + g_cad.vp3d_w;
    int dy1 = dy0 + g_cad.vp3d_h;

    gl.Disable(ZGL_DEPTH_TEST);
    gl.Disable(ZGL_CULL_FACE);
    gl.Disable(ZGL_POLYGON_OFFSET_FILL);
    if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);

    int cw = s->clip_x1 - s->clip_x0;
    int ch = s->clip_y1 - s->clip_y0;
    if (cw > 0 && ch > 0 && (s->clip_x0 > 0 || s->clip_y0 > 0 || s->clip_x1 < t->w || s->clip_y1 < t->h)) {
        gl.Enable(ZGL_SCISSOR_TEST);
        gl.Scissor(s->clip_x0, t->h - s->clip_y1, cw, ch);
    } else {
        gl.Disable(ZGL_SCISSOR_TEST);
    }

    gl.BindFramebuffer(ZGL_READ_FRAMEBUFFER, g_cad.fbo3d);
    gl.BindFramebuffer(ZGL_DRAW_FRAMEBUFFER, t->fbo);
    gl.BlitFramebuffer(0, 0, g_cad.vp3d_w, g_cad.vp3d_h,
                       dx0, dy0, dx1, dy1,
                       ZGL_COLOR_BUFFER_BIT, ZGL_NEAREST);
    gl.Disable(ZGL_SCISSOR_TEST);

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    gl.Viewport(0, 0, t->w, t->h);
    gl.BindVertexArray(0);
    gl.Enable(ZGL_BLEND);
    gl.BlendFuncSeparate(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA,
                         ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);
    zgl_invalidate_state();
    zgl_target_invalidate_base(t);
    t->opaque_dst = 0;
    t->gpu_ahead = 1;
    s->painted = 1;
}

/* --- 统一 GPU 硬件 Object-ID 拾取（2D + 3D，Scissor 局部窗口） --- */

static int zcad_ensure_pick_fbo(int w, int h) {
    if (g_cad.pick_fbo && g_cad.pick_w == w && g_cad.pick_h == h) return 1;
    zcad_drop_pick_fbo();
    gl.GenFramebuffers(1, &g_cad.pick_fbo);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, g_cad.pick_fbo);
    gl.GenRenderbuffers(1, &g_cad.pick_col_rb);
    gl.BindRenderbuffer(ZGL_RENDERBUFFER, g_cad.pick_col_rb);
    gl.RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_RGBA8, w, h);
    gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                               ZGL_RENDERBUFFER, g_cad.pick_col_rb);
    gl.GenRenderbuffers(1, &g_cad.pick_dep_rb);
    gl.BindRenderbuffer(ZGL_RENDERBUFFER, g_cad.pick_dep_rb);
    gl.RenderbufferStorage(ZGL_RENDERBUFFER, ZGL_DEPTH24_STENCIL8, w, h);
    gl.FramebufferRenderbuffer(ZGL_FRAMEBUFFER, ZGL_DEPTH_STENCIL_ATTACHMENT,
                               ZGL_RENDERBUFFER, g_cad.pick_dep_rb);
    if (gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) != ZGL_FRAMEBUFFER_COMPLETE) {
        zcad_drop_pick_fbo();
        return 0;
    }
    g_cad.pick_w = w;
    g_cad.pick_h = h;
    return 1;
}

EXPORT i32 zan_gui_cad_pick_begin(i32 surface_id, i32 px, i32 py, i32 radius_px) {
    zan_surface_t *s = zcad_get_surface(surface_id);
    if (!s || !zcad_surface_is_gl(s) || !zcad_ensure_init()) return 0;
    if (px < 0 || py < 0 || px >= s->width || py >= s->height) return 0;
    if (radius_px < 0) radius_px = 0;
    if (radius_px > 16) radius_px = 16;

    zgl_flush();
    if (!zcad_ensure_pick_fbo(s->width, s->height)) return 0;

    int gl_py = s->height - 1 - py;
    int sx0 = px - radius_px;
    int sy0 = gl_py - radius_px;
    int sx1 = px + radius_px + 1;
    int sy1 = gl_py + radius_px + 1;
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > s->width) sx1 = s->width;
    if (sy1 > s->height) sy1 = s->height;
    int sw = sx1 - sx0;
    int sh = sy1 - sy0;
    if (sw <= 0 || sh <= 0) return 0;

    gl.BindFramebuffer(ZGL_FRAMEBUFFER, g_cad.pick_fbo);
    gl.Viewport(0, 0, s->width, s->height);
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(sx0, sy0, sw, sh);
    gl.Disable(ZGL_BLEND);
    if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT | ZGL_DEPTH_BUFFER_BIT);

    g_cad.in_pick = 1;
    g_cad.pick_sid = surface_id;
    g_cad.pick_px = px;
    g_cad.pick_py = gl_py;
    g_cad.pick_radius = radius_px;
    g_cad.pick_sx = sx0;
    g_cad.pick_sy = sy0;
    g_cad.pick_sw = sw;
    g_cad.pick_sh = sh;
    g_cad.pick_3d_active = 0;
    return 1;
}

EXPORT void zan_gui_cad_pick_layer2d(i32 layer_id, double cam_x, double cam_y,
                                     double zoom, double rot_rad) {
    if (!g_cad.in_pick) return;
    zcad_layer2d_t *L = zcad_get_layer(layer_id);
    if (!L) return;
    gl.Viewport(0, 0, g_cad.pick_w, g_cad.pick_h);
    zcad_draw_layer2d_gl(L, g_cad.pick_w, g_cad.pick_h,
                         0.0f, 0.0f, (float)g_cad.pick_w, (float)g_cad.pick_h,
                         cam_x, cam_y, zoom, rot_rad, 1);
}

EXPORT void zan_gui_cad_pick_3d_begin(i32 vx, i32 vy, i32 vw, i32 vh,
                                      const float *view_proj_16) {
    if (!g_cad.in_pick || vw <= 0 || vh <= 0 || !view_proj_16) return;
    int gl_vy = g_cad.pick_h - (vy + vh);
    gl.Viewport(vx, gl_vy, vw, vh);
    gl.Enable(ZGL_DEPTH_TEST);
    gl.DepthFunc(ZGL_LESS);
    if (gl.DepthMask) gl.DepthMask(ZGL_TRUE);
    memcpy(g_cad.pick_vp_mat, view_proj_16, 16 * sizeof(float));
    g_cad.pick_3d_active = 1;
}

EXPORT void zan_gui_cad_pick_mesh3d(i32 mesh_id, const float *model_16, i32 entity_id) {
    if (!g_cad.in_pick || !g_cad.pick_3d_active || entity_id <= 0) return;
    zcad_mesh3d_t *M = zcad_get_mesh(mesh_id);
    if (!M) return;
    zcad_draw_mesh3d_internal(M, g_cad.pick_vp_mat, model_16,
                              0xFFFFFFFFu, 0xFFFFFFFFu, 1, entity_id, 1);
}

EXPORT i32 zan_gui_cad_pick_end(void) {
    if (!g_cad.in_pick) return 0;
    g_cad.in_pick = 0;

    int sw = g_cad.pick_sw, sh = g_cad.pick_sh;
    unsigned char buf[33 * 33 * 4];
    gl.PixelStorei(ZGL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(g_cad.pick_sx, g_cad.pick_sy, sw, sh,
                  ZGL_RGBA, ZGL_UNSIGNED_BYTE, buf);

    int best_id = 0;
    int best_d2 = 0x7fffffff;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            const unsigned char *px = buf + ((size_t)y * (size_t)sw + (size_t)x) * 4u;
            int id = (int)px[0] | ((int)px[1] << 8) | ((int)px[2] << 16);
            if (id == 0) continue;
            int dx = (g_cad.pick_sx + x) - g_cad.pick_px;
            int dy = (g_cad.pick_sy + y) - g_cad.pick_py;
            int d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_id = id;
            }
        }
    }

    gl.Disable(ZGL_SCISSOR_TEST);
    gl.Disable(ZGL_DEPTH_TEST);
    gl.Disable(ZGL_CULL_FACE);
    gl.BindVertexArray(0);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, 0);
    zgl_invalidate_state();
    return best_id;
}
