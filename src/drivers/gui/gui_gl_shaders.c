/* gui_gl_shaders.c -- GLSL 着色器源码与 OpenGL 渲染管线编译初始化 */

/* shaders */

static const char *ZGL_VS_BODY =
"uniform vec2 uViewport;\n"
"in vec2 a_pos;\n"
"in vec2 a_center;\n"
"in vec4 a_shape;\n"   /* halfW, halfH, radius, stroke */
"in vec4 a_kind;\n"    /* kind, p0, p1, p2 */
"in vec4 a_col0;\n"
"in vec4 a_col1;\n"
"in vec4 a_col2;\n"
"in vec4 a_clip;\n"    /* x0, y0, x1, y1 (exclusive) */
"in vec4 a_seg;\n"
"in vec2 a_uv;\n"
"flat out vec2 v_center;\n"
"flat out vec4 v_shape;\n"
"flat out vec4 v_kind;\n"
"flat out vec4 v_col0;\n"
"flat out vec4 v_col1;\n"
"flat out vec4 v_col2;\n"
"flat out vec4 v_clip;\n"
"flat out vec4 v_seg;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    v_center = a_center; v_shape = a_shape; v_kind = a_kind;\n"
"    v_col0 = a_col0; v_col1 = a_col1; v_col2 = a_col2;\n"
"    v_clip = a_clip; v_seg = a_seg; v_uv = a_uv;\n"
"    vec2 ndc = vec2(a_pos.x / uViewport.x * 2.0 - 1.0,\n"
"                    1.0 - a_pos.y / uViewport.y * 2.0);\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"}\n";

#define ZGL_FS_COMMON \
"uniform vec2 uViewport;\n" \
"flat in vec2 v_center;\n" \
"flat in vec4 v_shape;\n" \
"flat in vec4 v_kind;\n" \
"flat in vec4 v_col0;\n" \
"flat in vec4 v_col1;\n" \
"flat in vec4 v_col2;\n" \
"flat in vec4 v_clip;\n" \
"flat in vec4 v_seg;\n" \
"in vec2 v_uv;\n" \
"vec2 zpix() { return vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y); }\n" \
"void zclip(vec2 p) {\n" \
"    if (p.x < v_clip.x || p.y < v_clip.y || p.x >= v_clip.z || p.y >= v_clip.w)\n" \
"        discard;\n" \
"}\n"

static const char *ZGL_FS_SHAPE_BODY =
ZGL_FS_COMMON
"uniform sampler2D uCoverage;\n"
"uniform sampler2D uDestination;\n"
"uniform sampler2D uAtlas2;\n"
"vec4 over(vec4 src) {\n"
"    vec4 dst = texelFetch(uDestination, ivec2(gl_FragCoord.xy), 0);\n"
"    vec4 s = floor(clamp(src, 0.0, 1.0)*255.0 + 0.0001);\n"
"    vec4 d = floor(dst*255.0 + 0.5);\n"
"    if (s.a <= 0.0) return dst;\n"
"    float da = floor(d.a * (255.0-s.a) / 255.0);\n"
"    float a = s.a + da;\n"
"    \n"
"    return vec4(floor((s.rgb*s.a+d.rgb*da)/a), a)/255.0;\n"
"}\n"
"out vec4 o_color;\n"
"float sd_round(vec2 p, vec2 half_, float r, int mask) {\n"
"    float rr = r;\n"
"    int bit = (p.x < 0.0) ? ((p.y < 0.0) ? 1 : 8) : ((p.y < 0.0) ? 2 : 4);\n"
"    if ((mask & bit) == 0) rr = 0.0;\n"
"    vec2 q = abs(p) - half_ + rr;\n"
"    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rr;\n"
"}\n"
"float sd_seg(vec2 p, vec2 a, vec2 b) {\n"
"    vec2 pa = p - a, ba = b - a;\n"
"    float t = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);\n"
"    return length(pa - ba * t);\n"
"}\n"
"vec4 grad(float t) {\n"
"    if (v_kind.z > 0.5) {\n"
"        return (t < 0.5) ? mix(v_col0, v_col2, t * 2.0)\n"
"                         : mix(v_col2, v_col1, (t - 0.5) * 2.0);\n"
"    }\n"
"    return mix(v_col0, v_col1, t);\n"
"}\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
"    vec2 lp = p - v_center;\n"
"    if (kind == 2) lp -= vec2(0.5);\n"
"    float cov = 0.0;\n"
"    vec4 col = v_col0;\n"
"    if (kind == 0) {\n"
"        float sd = sd_round(lp, v_shape.xy, v_shape.z, int(v_kind.y + 0.5));\n"
"        if (v_shape.w > 0.0) sd = abs(sd + v_shape.w * 0.5) - v_shape.w * 0.5;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
"        int gdir = int(v_kind.w + 0.5);\n"
"        if (gdir >= 0) {\n"
"            vec2 h = v_shape.xy;\n"
"            float t;\n"
"            if (gdir == 1) t = (lp.x + h.x) / max(2.0 * h.x, 1.0);\n"
"            else if (gdir == 2) t = ((lp.x + h.x) + (lp.y + h.y))\n"
"                                    / max(2.0 * (h.x + h.y), 1.0);\n"
"            else if (gdir == 3) t = ((h.x - lp.x) + (lp.y + h.y))\n"
"                                    / max(2.0 * (h.x + h.y), 1.0);\n"
"            else t = (lp.y + h.y) / max(2.0 * h.y, 1.0);\n"
"            col = grad(clamp(t, 0.0, 1.0));\n"
"        }\n"
"    } else if (kind == 1) {\n"
"        float sd = (v_shape.w > 0.0)\n"
"                 ? abs(length(lp) - v_shape.z) - v_shape.w * 0.5\n"
"                 : length(lp) - v_shape.z;\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
"    } else if (kind == 2) {\n"
"        float r = v_shape.z;\n"
"        float d2 = dot(lp, lp);\n"
"        float t = (r * r - d2) / max(r * r, 1.0);\n"
"        if (t <= 0.0) discard;\n"
"        cov = t * t;\n"
"        col = vec4(v_col0.rgb, v_col0.a);\n"
"    } else if (kind == 3) {\n"
"        float dist = length(lp);\n"
"        float ri = v_kind.z, ro = v_shape.z;\n"
"        float radc = 1.0;\n"
"        if (dist > ro + 0.5) discard;\n"
"        if (ri > 0.0 && dist < ri - 0.5) discard;\n"
"        if (dist > ro - 0.5) radc = ro + 0.5 - dist;\n"
"        else if (ri > 0.0 && dist < ri + 0.5) radc = dist - (ri - 0.5);\n"
"        float a0 = v_kind.y, a1 = v_kind.w;\n"
"        float ang = degrees(atan(lp.x, -lp.y));\n"
"        if (ang < 0.0) ang += 360.0;\n"
"        float angc = 1.0;\n"
"        if (a1 - a0 < 360.0) {\n"
"            float dpix = (dist > 0.5) ? degrees(0.5 / dist) : 45.0;\n"
"            float aa = (ang < a0 - dpix) ? ang + 360.0 : ang;\n"
"            float lo = clamp((aa - a0 + dpix) / (2.0 * dpix), 0.0, 1.0);\n"
"            float hi = clamp((a1 - aa + dpix) / (2.0 * dpix), 0.0, 1.0);\n"
"            angc = lo * hi;\n"
"        }\n"
"        cov = clamp(radc, 0.0, 1.0) * angc;\n"
    "    } else if (kind == 4) {\n"
    "        float sd = sd_seg(p, v_seg.xy, v_seg.zw) - v_shape.z;\n"
    "        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
    "        if (v_kind.y > 0.5) cov = floor(cov * 255.0) / 255.0;\n"
    "    }\n"
    "    if (kind == 7) cov = texelFetch(uCoverage, ivec2(gl_FragCoord.xy), 0).r;\n"
"    if (kind == 8) {\n"
"        int mask = int(v_kind.y + 0.5);\n"
"        float outer = clamp(0.5 - sd_round(lp, v_shape.xy, v_shape.z, mask), 0.0, 1.0);\n"
"        float inner = outer;\n"
"        if (v_shape.w > 0.0) {\n"
"            vec2 h = v_shape.xy - v_shape.w;\n"
"            inner = (min(h.x, h.y) <= 0.0) ? 0.0 :\n"
"                clamp(0.5 - sd_round(lp, h, max(v_shape.z-v_shape.w, 0.0), mask), 0.0, outer);\n"
"        }\n"
"        float border = v_col1.a * (outer - inner);\n"
"        float fill = v_col0.a * (outer - border);\n"
"        float alpha = fill + border;\n"
"        if (alpha <= 0.0) discard;\n"
"        o_color = over(vec4((v_col0.rgb * fill + v_col1.rgb * border) / alpha, alpha));\n"
"        return;\n"
"    }\n"
"    if (kind == 10) {\n"
"        col = texture(uAtlas2, v_uv) * v_col0;\n"
"        cov = 1.0;\n"
"    }\n"
"    if (kind == 11) {\n"
"        float blur = v_shape.w;\n"
"        float d = sd_round(lp, v_shape.xy, v_shape.z, 15);\n"
"        if (blur <= 0.0) {\n"
"            cov = clamp(0.5 - d, 0.0, 1.0);\n"
"        } else {\n"
"            float t = (d + blur * 0.5) / blur;\n"
"            if (t <= 0.0) cov = 1.0;\n"
"            else if (t >= 1.0) discard;\n"
"            else cov = 1.0 - t * t * (3.0 - 2.0 * t);\n"
"        }\n"
"    }\n"
"    if (cov <= 0.0) discard;\n"
"    o_color = (kind == 4 && v_kind.y > 0.5) ? vec4(cov)\n"
"                                          : vec4(col.rgb, col.a * cov);\n"
"    \n"
"    if (kind == 7 || kind == 8) o_color = over(o_color);\n"
"}\n";

/* Desktop Core 3.3 Text Fragment: 亚像素 Dual-Source LCD 混合 */
static const char *ZGL_FS_TEXT_CORE_BODY =
ZGL_FS_COMMON
"uniform sampler2D uAtlas1;\n"
"uniform sampler2D uAtlas4;\n"
"layout(location = 0, index = 0) out vec4 o_color;\n"
"layout(location = 0, index = 1) out vec4 o_cov;\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
"    if (kind == 9) {\n"
"        vec4 t = texture(uAtlas4, v_uv);\n"
"        if (t.a <= 0.0) discard;\n"
"        o_color = vec4(t.rgb, 1.0);\n"
"        o_cov = vec4(t.a);\n"
"        return;\n"
"    }\n"
"    vec3 cov = (kind == 5) ? vec3(texture(uAtlas1, v_uv).r)\n"
"                           : texture(uAtlas4, v_uv).rgb;\n"
"    if (cov.r + cov.g + cov.b <= 0.0) discard;\n"
"    o_color = vec4(v_col0.rgb, 1.0);\n"
"    vec3 c = cov * v_col0.a;\n"
"    o_cov = vec4(c, max(max(c.r, c.g), c.b));\n"
"}\n";

/* GLES 3.0 Text Fragment: 标准高移植性 Alpha 混合输出 */
static const char *ZGL_FS_TEXT_ES_BODY =
ZGL_FS_COMMON
"uniform sampler2D uAtlas1;\n"
"uniform sampler2D uAtlas4;\n"
"out vec4 o_color;\n"
"void main() {\n"
"    vec2 p = zpix();\n"
"    zclip(p);\n"
"    int kind = int(v_kind.x + 0.5);\n"
"    if (kind == 9) {\n"
"        vec4 t = texture(uAtlas4, v_uv);\n"
"        if (t.a <= 0.0) discard;\n"
"        o_color = vec4(t.rgb, t.a * v_col0.a);\n"
"        return;\n"
"    }\n"
"    vec3 cov = (kind == 5) ? vec3(texture(uAtlas1, v_uv).r)\n"
"                           : texture(uAtlas4, v_uv).rgb;\n"
"    if (cov.r + cov.g + cov.b <= 0.0) discard;\n"
"    float a = (kind == 5) ? cov.r : max(max(cov.r, cov.g), cov.b);\n"
"    o_color = vec4(v_col0.rgb, v_col0.a * a);\n"
"}\n";

/* GL bootstrap */

static zgl_uint zgl_compile_parts(zgl_enum type, const char *header, const char *body) {
    zgl_uint sh = gl.CreateShader(type);
    const char *parts[2] = { header, body };
    zgl_int lens[2] = { (zgl_int)strlen(header), (zgl_int)strlen(body) };
    gl.ShaderSource(sh, 2, parts, lens);
    gl.CompileShader(sh);
    zgl_int ok = 0;
    gl.GetShaderiv(sh, ZGL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        zgl_sizei n = 0;
        gl.GetShaderInfoLog(sh, (zgl_sizei)sizeof(log), &n, log);
        fprintf(stderr, "[zan_gui] GL shader: %.*s\n", (int)n, log);
        gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static zgl_uint zgl_link_parts(const char *vs_head, const char *vs_body,
                               const char *fs_head, const char *fs_body) {
    zgl_uint vs = zgl_compile_parts(ZGL_VERTEX_SHADER, vs_head, vs_body);
    zgl_uint fs = zgl_compile_parts(ZGL_FRAGMENT_SHADER, fs_head, fs_body);
    if (!vs || !fs) return 0;
    zgl_uint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    zgl_int ok = 0;
    gl.GetProgramiv(prog, ZGL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        zgl_sizei n = 0;
        gl.GetProgramInfoLog(prog, (zgl_sizei)sizeof(log), &n, log);
        fprintf(stderr, "[zan_gui] GL link: %.*s\n", (int)n, log);
        gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

static zgl_uint zgl_link(const char *vs_src, const char *fs_src) {
    return zgl_link_parts("", vs_src, "", fs_src);
}

static void zgl_bind_attribs(zgl_uint prog) {
    struct { const char *name; int size; } a[] = {
        { "a_pos", 2 }, { "a_center", 2 }, { "a_shape", 4 }, { "a_kind", 4 },
        { "a_col0", 4 }, { "a_col1", 4 }, { "a_col2", 4 }, { "a_clip", 4 },
        { "a_seg", 4 }, { "a_uv", 2 },
    };
    size_t off = 0;
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++) {
        zgl_int loc = gl.GetAttribLocation(prog, a[i].name);
        if (loc >= 0) {
            gl.EnableVertexAttribArray((zgl_uint)loc);
            gl.VertexAttribPointer((zgl_uint)loc, a[i].size, ZGL_FLOAT,
                                   ZGL_FALSE, (zgl_sizei)(ZGL_VF * sizeof(float)),
                                   (const void *)(off * sizeof(float)));
        }
        off += (size_t)a[i].size;
    }
}

static zgl_uint zgl_new_atlas(zgl_enum internal, zgl_enum fmt, int dim) {
    zgl_uint t = 0;
    gl.GenTextures(1, &t);
    gl.BindTexture(ZGL_TEXTURE_2D, t);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, (zgl_int)internal, dim, dim, 0, fmt,
                  ZGL_UNSIGNED_BYTE, NULL);
    return t;
}

#define ZGL_ATLAS_DIM 1024

static int zgl_init(void) {
    if (g_gl_state) return g_gl_state > 0;
    g_gl_state = -1;
    if (!zan_gl_ctx_create()) return 0;
    if (!zan_gl_ctx_make_current()) { zan_gl_ctx_destroy(); return 0; }
    if (!zan_gl_api_load(&gl, zan_gl_ctx_getproc)) { zan_gl_ctx_destroy(); return 0; }
    zgl_blend_equation = (void (*)(zgl_enum))zan_gl_ctx_getproc("glBlendEquation");
    if (!zgl_blend_equation) { zan_gl_ctx_destroy(); return 0; }

    const char *ver = (const char *)gl.GetString(ZGL_VERSION);
    if ((ver && strstr(ver, "OpenGL ES"))
#if defined(__ANDROID__) || defined(__OHOS__)
        || 1
#endif
    ) {
        g_zgl.is_gles = 1;
    } else {
        g_zgl.is_gles = 0;
    }

    const char *vs_head = g_zgl.is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";
    const char *fs_head = g_zgl.is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";
    const char *fs_text_body = g_zgl.is_gles
        ? ZGL_FS_TEXT_ES_BODY
        : ZGL_FS_TEXT_CORE_BODY;

    g_zgl.prog_shape = zgl_link_parts(vs_head, ZGL_VS_BODY, fs_head, ZGL_FS_SHAPE_BODY);
    g_zgl.prog_text  = zgl_link_parts(vs_head, ZGL_VS_BODY, fs_head, fs_text_body);
    if (!g_zgl.prog_shape || !g_zgl.prog_text) { zan_gl_ctx_destroy(); return 0; }

    gl.GenVertexArrays(1, &g_zgl.vao);
    gl.GenBuffers(1, &g_zgl.vbo);
    gl.BindVertexArray(g_zgl.vao);
    gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo);
    zgl_bind_attribs(g_zgl.prog_shape);

    g_zgl.u_viewport_shape = gl.GetUniformLocation(g_zgl.prog_shape, "uViewport");
    g_zgl.u_viewport_text = gl.GetUniformLocation(g_zgl.prog_text, "uViewport");
    g_zgl.u_coverage = gl.GetUniformLocation(g_zgl.prog_shape, "uCoverage");
    g_zgl.u_destination = gl.GetUniformLocation(g_zgl.prog_shape, "uDestination");
    g_zgl.u_atlas2 = gl.GetUniformLocation(g_zgl.prog_shape, "uAtlas2");
    g_zgl.u_atlas1 = gl.GetUniformLocation(g_zgl.prog_text, "uAtlas1");
    g_zgl.u_atlas4 = gl.GetUniformLocation(g_zgl.prog_text, "uAtlas4");

    gl.PixelStorei(ZGL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(ZGL_PACK_ALIGNMENT, 1);
    zgl_enum color_fmt = g_zgl.is_gles ? ZGL_RGBA : ZGL_BGRA;
    g_zgl.atlas1 = zgl_new_atlas(ZGL_R8, ZGL_RED, ZGL_ATLAS_DIM);
    g_zgl.atlas4 = zgl_new_atlas(ZGL_RGBA8, color_fmt, ZGL_ATLAS_DIM);

    g_zgl.cap = (size_t)ZGL_VF * 6 * 2048;
    g_zgl.verts = (float *)malloc(g_zgl.cap * sizeof(float));
    if (!g_zgl.verts) { zan_gl_ctx_destroy(); return 0; }

    if (gl.GetError() != ZGL_NO_ERROR) { zan_gl_ctx_destroy(); return 0; }
    g_gl_state = 1;
    return 1;
}
