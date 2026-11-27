/* gui_gl_shaders.c -- GLSL 着色器源码与 OpenGL 渲染管线编译初始化 */

/* shaders */

static const char *ZGL_VS_BODY =
"uniform vec2 uViewport;\n"
"in vec4 a_rect;\n"    /* x0, y0, x1, y1 (clipped quad bounds) */
"in vec4 a_uvrect;\n"  /* u0, v0, u1, v1 */
"in vec2 a_center;\n"
"in vec4 a_shape;\n"   /* halfW, halfH, radius, stroke */
"in vec4 a_kind;\n"    /* kind, p0, p1, p2 */
"in vec4 a_col0;\n"
"in vec4 a_col1;\n"
"in vec4 a_col2;\n"
"in vec4 a_clip;\n"    /* x0, y0, x1, y1 (exclusive) */
"in vec4 a_seg;\n"
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
"    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
"    vec2 pos = mix(a_rect.xy, a_rect.zw, corner);\n"
"    v_uv = mix(a_uvrect.xy, a_uvrect.zw, corner);\n"
"    v_center = a_center; v_shape = a_shape; v_kind = a_kind;\n"
"    v_col0 = a_col0; v_col1 = a_col1; v_col2 = a_col2;\n"
"    v_clip = a_clip; v_seg = a_seg;\n"
"    vec2 ndc = vec2(pos.x / uViewport.x * 2.0 - 1.0,\n"
"                    1.0 - pos.y / uViewport.y * 2.0);\n"
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
"uniform sampler2D uAtlas1;\n"
"uniform sampler2D uAtlas4;\n"
"vec4 over(vec4 src) {\n"
"    vec4 dst = texelFetch(uDestination, ivec2(gl_FragCoord.xy), 0);\n"
"    vec4 s = floor(clamp(src, 0.0, 1.0)*255.0 + 0.0001);\n"
"    vec4 d = floor(dst*255.0 + 0.5);\n"
"    if (s.a <= 0.0) return dst;\n"
"    if (s.a >= 255.0) return s / 255.0;\n"
"    float da = floor(d.a * (255.0-s.a) / 255.0 + 0.0001);\n"
"    float a = s.a + da;\n"
"    return vec4(floor((s.rgb*s.a+d.rgb*da)/a + 0.0001), a)/255.0;\n"
"}\n"
"#if ZGL_DUAL_SOURCE\n"
"layout(location = 0, index = 0) out vec4 o_color;\n"
"layout(location = 0, index = 1) out vec4 o_cov;\n"
"void zemit(vec4 c) { o_color = c; o_cov = vec4(c.a); }\n"
"#else\n"
"out vec4 o_color;\n"
"void zemit(vec4 c) { o_color = c; }\n"
"#endif\n"
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
"    if (kind == 5 || kind == 6) {\n"
"        vec3 tcov = (kind == 5) ? vec3(texture(uAtlas1, v_uv).r)\n"
"                                : texture(uAtlas4, v_uv).rgb;\n"
"        if (tcov.r + tcov.g + tcov.b <= 0.0) discard;\n"
"        vec3 c = tcov * v_col0.a;\n"
"        float ca = max(max(c.r, c.g), c.b);\n"
"#if ZGL_DUAL_SOURCE\n"
"        o_color = vec4(v_col0.rgb, ca);\n"
"        o_cov = vec4(c, ca);\n"
"#else\n"
"        o_color = vec4(v_col0.rgb, ca);\n"
"#endif\n"
"        return;\n"
"    }\n"
"    if (kind == 9) {\n"
"        vec4 t = texture(uAtlas4, v_uv);\n"
"        if (t.a <= 0.0) discard;\n"
"        zemit(vec4(t.rgb, t.a * v_col0.a));\n"
"        return;\n"
"    }\n"
"    vec2 lp = p - v_center;\n"
"    if (kind == 2) lp -= vec2(0.5);\n"
"    float cov = 0.0;\n"
"    vec4 col = v_col0;\n"
"    if (kind == 0) {\n"
"        int gdir = int(v_kind.w + 0.5);\n"
"        if (v_shape.z <= 0.0 && v_shape.w <= 0.0) {\n"
"            if (gdir < 0) { zemit(v_col0); return; }\n"
"            cov = 1.0;\n"
"        } else {\n"
"            float sd = sd_round(lp, v_shape.xy, v_shape.z, int(v_kind.y + 0.5));\n"
"            if (v_shape.w > 0.0) sd = abs(sd + v_shape.w * 0.5) - v_shape.w * 0.5;\n"
"            cov = clamp(0.5 - sd, 0.0, 1.0);\n"
"        }\n"
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
"    } else if (kind == 7) {\n"
"        cov = texelFetch(uCoverage, ivec2(gl_FragCoord.xy), 0).r;\n"
"    } else if (kind == 8 || kind == 12) {\n"
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
"        vec4 src = vec4((v_col0.rgb * fill + v_col1.rgb * border) / alpha, alpha);\n"
"        zemit((kind == 8) ? over(src) : src);\n"
"        return;\n"
"    } else if (kind == 10) {\n"
"        col = ((v_kind.y > 0.5) ? texture(uAtlas4, v_uv) : texture(uAtlas2, v_uv)) * v_col0;\n"
"        cov = 1.0;\n"
"    } else if (kind == 13) {\n"
"        int sw = int(v_shape.z + 0.5);\n"
"        int sh = int(v_shape.w + 0.5);\n"
"        int texH = int(v_kind.y + 0.5);\n"
"        if (v_kind.w > 0.5) {\n"
"            int ix = int(floor(lp.x + v_shape.x));\n"
"            int iy = int(floor(lp.y + v_shape.y));\n"
"            if (ix < 0 || iy < 0 || ix >= sw || iy >= sh) discard;\n"
"            vec4 c = texelFetch(uAtlas2, ivec2(ix, texH - 1 - iy), 0);\n"
"            float oa = c.a * v_kind.z;\n"
"            if (oa <= 0.001) discard;\n"
"            zemit(vec4(c.rgb, oa));\n"
"            return;\n"
"        }\n"
"        float sx = lp.x * v_seg.x + lp.y * v_seg.y + v_shape.x - 0.5;\n"
"        float sy = lp.x * v_seg.z + lp.y * v_seg.w + v_shape.y - 0.5;\n"
"        if (sx < -1.0 || sy < -1.0 || sx >= float(sw) || sy >= float(sh)) discard;\n"
"        int ix = int(floor(sx));\n"
"        int iy = int(floor(sy));\n"
"        float fx = sx - float(ix);\n"
"        float fy = sy - float(iy);\n"
"        float w00 = (1.0 - fx) * (1.0 - fy);\n"
"        float w10 = fx * (1.0 - fy);\n"
"        float w01 = (1.0 - fx) * fy;\n"
"        float w11 = fx * fy;\n"
"        vec3 rgbSum = vec3(0.0);\n"
"        float aSum = 0.0;\n"
"        if (ix >= 0 && iy >= 0 && ix < sw && iy < sh) {\n"
"            vec4 c = texelFetch(uAtlas2, ivec2(ix, texH - 1 - iy), 0);\n"
"            float wa = c.a * w00; aSum += wa; rgbSum += c.rgb * wa;\n"
"        }\n"
"        if (ix + 1 >= 0 && iy >= 0 && ix + 1 < sw && iy < sh) {\n"
"            vec4 c = texelFetch(uAtlas2, ivec2(ix + 1, texH - 1 - iy), 0);\n"
"            float wa = c.a * w10; aSum += wa; rgbSum += c.rgb * wa;\n"
"        }\n"
"        if (ix >= 0 && iy + 1 >= 0 && ix < sw && iy + 1 < sh) {\n"
"            vec4 c = texelFetch(uAtlas2, ivec2(ix, texH - 2 - iy), 0);\n"
"            float wa = c.a * w01; aSum += wa; rgbSum += c.rgb * wa;\n"
"        }\n"
"        if (ix + 1 >= 0 && iy + 1 >= 0 && ix + 1 < sw && iy + 1 < sh) {\n"
"            vec4 c = texelFetch(uAtlas2, ivec2(ix + 1, texH - 2 - iy), 0);\n"
"            float wa = c.a * w11; aSum += wa; rgbSum += c.rgb * wa;\n"
"        }\n"
"        if (aSum <= 0.0019) discard;\n"
"        zemit(vec4(rgbSum / aSum, clamp(aSum * v_kind.z, 0.0, 1.0)));\n"
"        return;\n"
"    } else if (kind == 11) {\n"
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
"    vec4 res = (kind == 4 && v_kind.y > 0.5) ? vec4(cov)\n"
"                                             : vec4(col.rgb, col.a * cov);\n"
"    if (kind == 7) res = over(res);\n"
"    zemit(res);\n"
"}\n";

/* GPU Dual-Kawase Blur shaders (GL 3.3 Core & GLES 3.0 compatible) */
static const char *ZGL_VS_BLUR_BODY =
"uniform vec4 uUvRect;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    vec2 p = vec2((gl_VertexID == 1 || gl_VertexID == 4 || gl_VertexID == 5) ? 1.0 : 0.0,\n"
"                  (gl_VertexID == 2 || gl_VertexID == 3 || gl_VertexID == 5) ? 1.0 : 0.0);\n"
"    v_uv = mix(uUvRect.xy, uUvRect.zw, p);\n"
"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
"}\n";

static const char *ZGL_FS_KAWASE_BODY =
"uniform sampler2D uTex;\n"
"uniform vec2 uStep;\n"
"uniform vec4 uClamp;\n"
"uniform int uMode;\n"
"in vec2 v_uv;\n"
"out vec4 o_color;\n"
"vec4 smp(vec2 uv) { return texture(uTex, clamp(uv, uClamp.xy, uClamp.zw)); }\n"
"void main() {\n"
"    if (uMode == 0) {\n"
"        vec4 sum = smp(v_uv) * 4.0;\n"
"        sum += smp(v_uv + vec2(-uStep.x, -uStep.y));\n"
"        sum += smp(v_uv + vec2( uStep.x, -uStep.y));\n"
"        sum += smp(v_uv + vec2(-uStep.x,  uStep.y));\n"
"        sum += smp(v_uv + vec2( uStep.x,  uStep.y));\n"
"        o_color = sum * 0.125;\n"
"    } else {\n"
"        vec4 sum = smp(v_uv + vec2(-uStep.x * 2.0, 0.0));\n"
"        sum += smp(v_uv + vec2( uStep.x * 2.0, 0.0));\n"
"        sum += smp(v_uv + vec2(0.0, -uStep.y * 2.0));\n"
"        sum += smp(v_uv + vec2(0.0,  uStep.y * 2.0));\n"
"        sum += smp(v_uv + vec2(-uStep.x, -uStep.y)) * 2.0;\n"
"        sum += smp(v_uv + vec2( uStep.x, -uStep.y)) * 2.0;\n"
"        sum += smp(v_uv + vec2(-uStep.x,  uStep.y)) * 2.0;\n"
"        sum += smp(v_uv + vec2( uStep.x,  uStep.y)) * 2.0;\n"
"        o_color = sum / 12.0;\n"
"    }\n"
"}\n";

static const char *ZGL_VS_BLUR_COMP_BODY =
"uniform vec2 uViewport;\n"
"uniform vec4 uRect;\n"
"uniform vec4 uUvRect;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    vec2 p = vec2((gl_VertexID == 1 || gl_VertexID == 4 || gl_VertexID == 5) ? 1.0 : 0.0,\n"
"                  (gl_VertexID == 2 || gl_VertexID == 3 || gl_VertexID == 5) ? 1.0 : 0.0);\n"
"    vec2 px = uRect.xy + p * uRect.zw;\n"
"    v_uv = vec2(mix(uUvRect.x, uUvRect.z, p.x), mix(uUvRect.w, uUvRect.y, p.y));\n"
"    vec2 ndc = vec2(px.x / uViewport.x * 2.0 - 1.0,\n"
"                    1.0 - px.y / uViewport.y * 2.0);\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"}\n";

static const char *ZGL_FS_BLUR_COMP_BODY =
"uniform sampler2D uTex;\n"
"uniform vec2 uViewport;\n"
"uniform vec4 uRect;\n"
"uniform vec4 uShape;\n"
"uniform vec4 uClip;\n"
"in vec2 v_uv;\n"
"out vec4 o_color;\n"
"float sd_round(vec2 p, vec2 half_, float r, int mask) {\n"
"    float rr = r;\n"
"    int bit = (p.x < 0.0) ? ((p.y < 0.0) ? 1 : 8) : ((p.y < 0.0) ? 2 : 4);\n"
"    if ((mask & bit) == 0) rr = 0.0;\n"
"    vec2 q = abs(p) - half_ + rr;\n"
"    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rr;\n"
"}\n"
"void main() {\n"
"    vec2 p = vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y);\n"
"    if (p.x < uClip.x || p.y < uClip.y || p.x >= uClip.z || p.y >= uClip.w) discard;\n"
"    float cov = 1.0;\n"
"    if (uShape.x > 0.0) {\n"
"        vec2 half_ = uRect.zw * 0.5;\n"
"        vec2 lp = p - (uRect.xy + half_);\n"
"        float sd = sd_round(lp, half_, uShape.x, int(uShape.y + 0.5));\n"
"        cov = clamp(0.5 - sd, 0.0, 1.0);\n"
"        if (cov <= 0.0) discard;\n"
"    }\n"
"    vec4 c = texture(uTex, v_uv);\n"
"    o_color = vec4(c.rgb, cov);\n"
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
        { "a_rect", 4 }, { "a_uvrect", 4 }, { "a_center", 2 }, { "a_shape", 4 },
        { "a_kind", 4 }, { "a_col0", 4 }, { "a_col1", 4 }, { "a_col2", 4 },
        { "a_clip", 4 }, { "a_seg", 4 },
    };
    size_t off = 0;
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++) {
        zgl_int loc = gl.GetAttribLocation(prog, a[i].name);
        if (loc >= 0) {
            gl.EnableVertexAttribArray((zgl_uint)loc);
            gl.VertexAttribPointer((zgl_uint)loc, a[i].size, ZGL_FLOAT,
                                   ZGL_FALSE, (zgl_sizei)(ZGL_IF * sizeof(float)),
                                   (const void *)(off * sizeof(float)));
            gl.VertexAttribDivisor((zgl_uint)loc, 1);
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
    if (!gl.DrawArraysInstanced || !gl.VertexAttribDivisor) { zan_gl_ctx_destroy(); return 0; }
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
    const char *fs_uber_head = g_zgl.is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n#define ZGL_DUAL_SOURCE 0\n"
        : "#version 330 core\n#define ZGL_DUAL_SOURCE 1\n";

    g_zgl.prog_shape = zgl_link_parts(vs_head, ZGL_VS_BODY, fs_uber_head, ZGL_FS_SHAPE_BODY);
    if (!g_zgl.prog_shape) { zan_gl_ctx_destroy(); return 0; }

    g_zgl.prog_blur = zgl_link_parts(vs_head, ZGL_VS_BLUR_BODY, fs_head, ZGL_FS_KAWASE_BODY);
    g_zgl.prog_blur_comp = zgl_link_parts(vs_head, ZGL_VS_BLUR_COMP_BODY, fs_head, ZGL_FS_BLUR_COMP_BODY);
    if (g_zgl.prog_blur && g_zgl.prog_blur_comp) {
        gl.GenVertexArrays(1, &g_zgl.blur_vao);
        g_zgl.u_blur_tex = gl.GetUniformLocation(g_zgl.prog_blur, "uTex");
        g_zgl.u_blur_uvrect = gl.GetUniformLocation(g_zgl.prog_blur, "uUvRect");
        g_zgl.u_blur_step = gl.GetUniformLocation(g_zgl.prog_blur, "uStep");
        g_zgl.u_blur_clamp = gl.GetUniformLocation(g_zgl.prog_blur, "uClamp");
        g_zgl.u_blur_mode = gl.GetUniformLocation(g_zgl.prog_blur, "uMode");
        g_zgl.u_bcomp_tex = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uTex");
        g_zgl.u_bcomp_vp = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uViewport");
        g_zgl.u_bcomp_rect = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uRect");
        g_zgl.u_bcomp_uvrect = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uUvRect");
        g_zgl.u_bcomp_shape = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uShape");
        g_zgl.u_bcomp_clip = gl.GetUniformLocation(g_zgl.prog_blur_comp, "uClip");
    }

    gl.GenVertexArrays(ZGL_RING_SLOTS, g_zgl.vao);
    gl.GenBuffers(ZGL_RING_SLOTS, g_zgl.vbo);
    for (int i = 0; i < ZGL_RING_SLOTS; i++) {
        gl.BindVertexArray(g_zgl.vao[i]);
        gl.BindBuffer(ZGL_ARRAY_BUFFER, g_zgl.vbo[i]);
        g_zgl.vbo_cap_quads[i] = 2048;
        gl.BufferData(ZGL_ARRAY_BUFFER,
                      (zgl_sizeiptr)(g_zgl.vbo_cap_quads[i] * (size_t)ZGL_IF * sizeof(float)),
                      NULL, ZGL_STREAM_DRAW);
        zgl_bind_attribs(g_zgl.prog_shape);
    }
    gl.BindVertexArray(g_zgl.vao[0]);
    g_zgl.ring_idx = 0;

    g_zgl.u_viewport_shape = gl.GetUniformLocation(g_zgl.prog_shape, "uViewport");
    g_zgl.u_coverage = gl.GetUniformLocation(g_zgl.prog_shape, "uCoverage");
    g_zgl.u_destination = gl.GetUniformLocation(g_zgl.prog_shape, "uDestination");
    g_zgl.u_atlas2 = gl.GetUniformLocation(g_zgl.prog_shape, "uAtlas2");
    g_zgl.u_atlas1 = gl.GetUniformLocation(g_zgl.prog_shape, "uAtlas1");
    g_zgl.u_atlas4 = gl.GetUniformLocation(g_zgl.prog_shape, "uAtlas4");

    /* Bind constant sampler texture units once for the lifetime of each program. */
    gl.UseProgram(g_zgl.prog_shape);
    if (g_zgl.u_coverage >= 0) gl.Uniform1i(g_zgl.u_coverage, 0);
    if (g_zgl.u_destination >= 0) gl.Uniform1i(g_zgl.u_destination, 1);
    if (g_zgl.u_atlas2 >= 0) gl.Uniform1i(g_zgl.u_atlas2, 2);
    if (g_zgl.u_atlas1 >= 0) gl.Uniform1i(g_zgl.u_atlas1, 3);
    if (g_zgl.u_atlas4 >= 0) gl.Uniform1i(g_zgl.u_atlas4, 4);
    if (g_zgl.prog_blur && g_zgl.u_blur_tex >= 0) {
        gl.UseProgram(g_zgl.prog_blur);
        gl.Uniform1i(g_zgl.u_blur_tex, 0);
    }
    if (g_zgl.prog_blur_comp && g_zgl.u_bcomp_tex >= 0) {
        gl.UseProgram(g_zgl.prog_blur_comp);
        gl.Uniform1i(g_zgl.u_bcomp_tex, 0);
    }

    gl.PixelStorei(ZGL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(ZGL_PACK_ALIGNMENT, 1);
    zgl_enum color_fmt = g_zgl.is_gles ? ZGL_RGBA : ZGL_BGRA;
    g_zgl.atlas1 = zgl_new_atlas(ZGL_R8, ZGL_RED, ZGL_ATLAS_DIM);
    g_zgl.atlas4 = zgl_new_atlas(ZGL_RGBA8, color_fmt, ZGL_ATLAS_DIM);

    g_zgl.cap = (size_t)ZGL_IF * 2048;
    g_zgl.verts = (float *)malloc(g_zgl.cap * sizeof(float));
    if (!g_zgl.verts) { zan_gl_ctx_destroy(); return 0; }

    if (gl.GetError() != ZGL_NO_ERROR) { zan_gl_ctx_destroy(); return 0; }
    g_gl_state = 1;
    return 1;
}
