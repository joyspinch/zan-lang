/* gui_gl_draw.c -- OpenGL 2D 几何与矢量图元栅格化（矩形、圆角、渐变、阴影、圆/弧、多段线） */

static void gl_clear_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (w <= 0 || h <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_REPLACE)) { cpu_clear_rect(s, x, y, w, h, c); return; }
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
}

static void gl_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (w <= 0 || h <= 0) return;
    if (((c >> 24) & 0xFF) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) { cpu_fill_rect(s, x, y, w, h, c); return; }
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
}

static void gl_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 c) {
    if (w <= 0 || h <= 0) return;
    if (((c >> 24) & 0xFF) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_round(s, x, y, w, h, radius, corners, c); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, -1, c, c, 0, 0);
}

static void gl_draw_round(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 c, int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    if (((c >> 24) & 0xFF) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_round(s, x, y, w, h, radius, corners, c, thickness); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, thickness, -1, c, c, 0, 0);
}

static void gl_surface_round(zan_surface_t *s, int x, int y, int w, int h,
                             int radius, int corners, u32 fill, u32 border, int thickness) {
    if (w <= 0 || h <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_surface_round(s, x, y, w, h, radius, corners, fill, border, thickness);
        return;
    }
    if (g_zgl.count) zgl_flush();
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_SURFACE;
    q.cx = (float)x + (float)w * 0.5f;
    q.cy = (float)y + (float)h * 0.5f;
    q.hw = (float)w * 0.5f; q.hh = (float)h * 0.5f;
    int r = radius < 0 ? 0 : radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    q.radius = (float)r;
    q.stroke = thickness > 0 ? (float)thickness : 0.0f;
    q.p0 = (float)(corners & 15);
    zgl_color(q.col0, fill, 0); zgl_color(q.col1, border, 0);
    zgl_push(s, &q, (float)x, (float)y, (float)x + (float)w, (float)y + (float)h);
    zgl_flush();
}

static void gl_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 top, u32 bottom) {
    int rounded = (radius > 0 && (corners & 15) != 0);
    if (!zgl_begin(s, rounded ? ZGL_MODE_BLEND : ZGL_MODE_REPLACE)) {
        cpu_fill_vgrad(s, x, y, w, h, radius, corners, top, bottom); return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, ZAN_GRAD_VERTICAL,
                  top, bottom, 0, 1);
}

static void gl_fill_grad(zan_surface_t *s, int x, int y, int w, int h,
                         int radius, int corners, int dir,
                         u32 from, u32 via, u32 to) {
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_grad(s, x, y, w, h, radius, corners, dir, from, via, to);
        return;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, dir, from, to, via, 0);
}

static void gl_shadow_round(zan_surface_t *s, int x, int y, int w, int h,
                            int radius, int blur, u32 c) {
    if (w <= 0 || h <= 0 || (c >> 24) == 0) return;
    if (blur <= 0) {
        gl_fill_round(s, x, y, w, h, radius, ZAN_CORNERS_ALL, c);
        return;
    }
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_shadow_round(s, x, y, w, h, radius, blur, c);
        return;
    }
    int r = radius < 0 ? 0 : radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_SHADOW;
    q.cx = (float)x + (float)w * 0.5f;
    q.cy = (float)y + (float)h * 0.5f;
    q.hw = (float)w * 0.5f;
    q.hh = (float)h * 0.5f;
    q.radius = (float)r;
    q.stroke = (float)blur;
    q.p0 = 15.0f;
    zgl_color(q.col0, c, 0);

    float pad = (float)blur;
    zgl_push(s, &q, (float)x - pad, (float)y - pad, (float)(x + w) + pad, (float)(y + h) + pad);
}

static void gl_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                      int stroke) {
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_CIRCLE;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)radius;
    q.stroke = (float)stroke;
    q.hw = (float)radius + (float)stroke * 0.5f + 2.0f;
    q.hh = q.hw;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_fill_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c) {
    if (radius <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_circle(s, cx, cy, radius, c); return;
    }
    gl_circle(s, cx, cy, radius, c, 0);
}

static void gl_draw_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                           int thickness) {
    if (radius <= 0 || thickness <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_circle(s, cx, cy, radius, c, thickness); return;
    }
    gl_circle(s, cx, cy, radius, c, thickness);
}

static void gl_fill_radial(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                           int inner_alpha) {
    if (radius <= 0 || inner_alpha <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_radial(s, cx, cy, radius, c, inner_alpha); return;
    }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_RADIAL;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)radius;
    q.hw = (float)radius + 1.0f;
    q.hh = (float)radius + 1.0f;
    zgl_color(q.col0, (c & 0x00FFFFFFu) |
              ((u32)(inner_alpha > 255 ? 255 : inner_alpha) << 24), 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                           int r_outer, int a0_deg, int a1_deg, u32 c) {
    if (r_outer <= 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_sector(s, cx, cy, r_inner, r_outer, a0_deg, a1_deg, c); return;
    }
    int a0 = a0_deg, a1 = a1_deg;
    if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_SECTOR;
    q.cx = (float)cx;
    q.cy = (float)cy;
    q.radius = (float)r_outer;
    q.p0 = (float)a0;
    q.p1 = (float)r_inner;
    q.p2 = (float)a1;
    q.hw = (float)r_outer + 2.0f;
    q.hh = (float)r_outer + 2.0f;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, q.cx - q.hw, q.cy - q.hh, q.cx + q.hw, q.cy + q.hh);
}

static void gl_capsule(zan_surface_t *s, float x0, float y0, float x1, float y1,
                       float half, u32 c) {
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_CAPSULE;
    q.p0 = g_zgl.mode == ZGL_MODE_UNION ? 1.0f : 0.0f;
    q.radius = half;
    q.seg[0] = x0; q.seg[1] = y0; q.seg[2] = x1; q.seg[3] = y1;
    float lo_x = (x0 < x1 ? x0 : x1) - half - 1.0f;
    float hi_x = (x0 > x1 ? x0 : x1) + half + 1.0f;
    float lo_y = (y0 < y1 ? y0 : y1) - half - 1.0f;
    float hi_y = (y0 > y1 ? y0 : y1) + half + 1.0f;
    q.cx = (lo_x + hi_x) / 2.0f;
    q.cy = (lo_y + hi_y) / 2.0f;
    q.hw = (hi_x - lo_x) / 2.0f;
    q.hh = (hi_y - lo_y) / 2.0f;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
}

static void gl_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                         u32 c, int thickness) {
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_draw_line(s, x0, y0, x1, y1, c, thickness); return;
    }
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    float offset = thickness > 1 ? 0.0f : 0.5f;
    gl_capsule(s, (float)x0 + offset, (float)y0 + offset,
               (float)x1 + offset, (float)y1 + offset, half, c);
}

static int zgl_union_target(zgl_target *t) {
    if (t->cov_fbo) return 1;
    gl.ActiveTexture(ZGL_TEXTURE0);
    gl.GenTextures(1, &t->cov_tex);
    gl.BindTexture(ZGL_TEXTURE_2D, t->cov_tex);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, ZGL_NEAREST);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_S, ZGL_CLAMP_TO_EDGE);
    gl.TexParameteri(ZGL_TEXTURE_2D, ZGL_TEXTURE_WRAP_T, ZGL_CLAMP_TO_EDGE);
    gl.TexImage2D(ZGL_TEXTURE_2D, 0, ZGL_R8, t->w, t->h, 0,
                  ZGL_RED, ZGL_UNSIGNED_BYTE, NULL);
    gl.GenFramebuffers(1, &t->cov_fbo);
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.FramebufferTexture2D(ZGL_FRAMEBUFFER, ZGL_COLOR_ATTACHMENT0,
                            ZGL_TEXTURE_2D, t->cov_tex, 0);
    int ok = t->cov_tex && t->cov_fbo &&
             gl.CheckFramebufferStatus(ZGL_FRAMEBUFFER) == ZGL_FRAMEBUFFER_COMPLETE;
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->fbo);
    if (!ok) {
        gl.DeleteFramebuffers(1, &t->cov_fbo);
        gl.DeleteTextures(1, &t->cov_tex);
        t->cov_fbo = t->cov_tex = 0;
    }
    return ok;
}

static void gl_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                        int thickness, int fx) {
    if (!pts || n < 2 || (c >> 24) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_polyline(s, pts, n, c, thickness, fx);
        return;
    }
    float scale = fx == 0 ? 1.0f : 1.0f / 256.0f;
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    float lo_x = (float)s->width, lo_y = (float)s->height, hi_x = 0, hi_y = 0;
    int segments = 0;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale, ay = (float)pts[i * 2 + 1] * scale;
        float bx = (float)pts[i * 2 + 2] * scale, by = (float)pts[i * 2 + 3] * scale;
        float dx = bx - ax, dy = by - ay;
        if (dx * dx + dy * dy < 0.0001f) continue;
        segments++;
        float pad = half + 1.0f;
        lo_x = fminf(lo_x, fminf(ax, bx) - pad);
        lo_y = fminf(lo_y, fminf(ay, by) - pad);
        hi_x = fmaxf(hi_x, fmaxf(ax, bx) + pad);
        hi_y = fmaxf(hi_y, fmaxf(ay, by) + pad);
    }
    lo_x = fmaxf(lo_x, fmaxf(0.0f, (float)s->clip_x0));
    lo_y = fmaxf(lo_y, fmaxf(0.0f, (float)s->clip_y0));
    hi_x = fminf(hi_x, fminf((float)s->width, (float)s->clip_x1));
    hi_y = fminf(hi_y, fminf((float)s->height, (float)s->clip_y1));
    if (!segments || lo_x >= hi_x || lo_y >= hi_y) return;
    int clear_x = (int)floorf(lo_x), clear_y = (int)floorf(lo_y);
    int clear_x1 = (int)ceilf(hi_x), clear_y1 = (int)ceilf(hi_y);
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        fprintf(stderr, "[zan_gui] GL polyline coverage framebuffer unavailable; CPU fallback\n");
        gl_sync_to_cpu(s);
        cpu_polyline(s, pts, n, c, thickness, fx);
        return;
    }
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(clear_x, t->h - clear_y1, clear_x1 - clear_x, clear_y1 - clear_y);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT);
    gl.Disable(ZGL_SCISSOR_TEST);
    g_zgl.mode = ZGL_MODE_UNION;
    for (int i = 0; i + 1 < n; i++) {
        float ax = (float)pts[i * 2] * scale;
        float ay = (float)pts[i * 2 + 1] * scale;
        float bx = (float)pts[i * 2 + 2] * scale;
        float by = (float)pts[i * 2 + 3] * scale;
        float dx = bx - ax, dy = by - ay;
        if (dx * dx + dy * dy < 0.0001f) continue;
        gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
    }
    zgl_flush();
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush();
}

static void cpu_polybatch_loop(zan_surface_t *s, const int32_t *pts,
                               const int32_t *counts, int n_paths, u32 c,
                               int thickness, int fx) {
    int off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        if (n >= 2) cpu_polyline(s, pts + off, n, c, thickness, fx);
        off += n * 2;
    }
}

static void gl_polybatch(zan_surface_t *s, const int32_t *pts,
                         const int32_t *counts, int n_paths, u32 c,
                         int thickness, int fx) {
    if (!pts || !counts || n_paths < 1 || (c >> 24) == 0) return;
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        gl_sync_to_cpu(s);
        cpu_polybatch_loop(s, pts, counts, n_paths, c, thickness, fx);
        return;
    }
    float scale = fx == 0 ? 1.0f : 1.0f / 256.0f;
    float half = (thickness > 1) ? (float)thickness / 2.0f : 0.5f;
    float lo_x = (float)s->width, lo_y = (float)s->height, hi_x = 0, hi_y = 0;
    int segments = 0;
    int off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        for (int i = 0; n >= 2 && i + 1 < n; i++) {
            float ax = (float)pts[off + i * 2] * scale;
            float ay = (float)pts[off + i * 2 + 1] * scale;
            float bx = (float)pts[off + i * 2 + 2] * scale;
            float by = (float)pts[off + i * 2 + 3] * scale;
            float dx = bx - ax, dy = by - ay;
            if (dx * dx + dy * dy < 0.0001f) continue;
            segments++;
            float pad = half + 1.0f;
            lo_x = fminf(lo_x, fminf(ax, bx) - pad);
            lo_y = fminf(lo_y, fminf(ay, by) - pad);
            hi_x = fmaxf(hi_x, fmaxf(ax, bx) + pad);
            hi_y = fmaxf(hi_y, fmaxf(ay, by) + pad);
        }
        off += n * 2;
    }
    lo_x = fmaxf(lo_x, fmaxf(0.0f, (float)s->clip_x0));
    lo_y = fmaxf(lo_y, fmaxf(0.0f, (float)s->clip_y0));
    hi_x = fminf(hi_x, fminf((float)s->width, (float)s->clip_x1));
    hi_y = fminf(hi_y, fminf((float)s->height, (float)s->clip_y1));
    if (!segments || lo_x >= hi_x || lo_y >= hi_y) return;
    int clear_x = (int)floorf(lo_x), clear_y = (int)floorf(lo_y);
    int clear_x1 = (int)ceilf(hi_x), clear_y1 = (int)ceilf(hi_y);
    zgl_flush();
    zgl_target *t = zgl_target_of(s);
    if (!zgl_union_target(t)) {
        fprintf(stderr, "[zan_gui] GL polybatch coverage framebuffer unavailable; CPU fallback\n");
        gl_sync_to_cpu(s);
        cpu_polybatch_loop(s, pts, counts, n_paths, c, thickness, fx);
        return;
    }
    gl.BindFramebuffer(ZGL_FRAMEBUFFER, t->cov_fbo);
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(clear_x, t->h - clear_y1, clear_x1 - clear_x, clear_y1 - clear_y);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT);
    gl.Disable(ZGL_SCISSOR_TEST);
    g_zgl.mode = ZGL_MODE_UNION;
    off = 0;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        for (int i = 0; n >= 2 && i + 1 < n; i++) {
            float ax = (float)pts[off + i * 2] * scale;
            float ay = (float)pts[off + i * 2 + 1] * scale;
            float bx = (float)pts[off + i * 2 + 2] * scale;
            float by = (float)pts[off + i * 2 + 3] * scale;
            float dx = bx - ax, dy = by - ay;
            if (dx * dx + dy * dy < 0.0001f) continue;
            gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
        }
        off += n * 2;
    }
    zgl_flush();
    g_zgl.mode = ZGL_MODE_BLEND;
    zgl_quad q;
    memset(&q, 0, sizeof(q));
    q.kind = ZGL_K_UNION;
    zgl_color(q.col0, c, 0);
    zgl_push(s, &q, lo_x, lo_y, hi_x, hi_y);
    zgl_flush();
}
