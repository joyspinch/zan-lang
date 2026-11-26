/* gui_gl_draw.c -- OpenGL 2D 几何与矢量图元栅格化（矩形、圆角、渐变、阴影、圆/弧、多段线） */

static int zgl_covers_full_surface(const zan_surface_t *s, int x, int y, int w, int h) {
    return x <= 0 && y <= 0 && x + w >= s->width && y + h >= s->height
        && s->clip_x0 <= 0 && s->clip_y0 <= 0
        && s->clip_x1 >= s->width && s->clip_y1 >= s->height;
}

static void gl_clear_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (w <= 0 || h <= 0) return;
    int opaque = (((c >> 24) & 0xFF) == 255);
    int full = zgl_covers_full_surface(s, x, y, w, h);
    if (full && g_gl_state > 0) {
        zgl_target *t0 = zgl_target_of(s);
        if (t0) t0->cpu_ahead = 0;
    }
    zgl_mode want = (opaque && g_zgl.count && g_zgl.target == s && g_zgl.mode == ZGL_MODE_BLEND)
                    ? ZGL_MODE_BLEND : ZGL_MODE_REPLACE;
    if (!zgl_begin(s, want)) { cpu_clear_rect(s, x, y, w, h, c); return; }
    zgl_target *t = zgl_target_of(s);
    if (t) {
        if (!opaque) {
            t->opaque_dst = 0;
        } else if (full) {
            t->opaque_dst = 1;
        }
    }
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
    if (t && full && (opaque || want == ZGL_MODE_REPLACE)) {
        zgl_target_reset_base_clear(t, c);
    }
}

static void gl_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    if (w <= 0 || h <= 0) return;
    u32 a = (c >> 24) & 0xFF;
    if (a == 0) return;
    if (a == 255 && g_gl_state > 0) {
        zgl_target *t0 = zgl_target_of(s);
        if (t0 && !t0->cpu_ahead && t0->base_kind == 1 && t0->base_color == c) {
            int cx0 = x > s->clip_x0 ? x : s->clip_x0;
            int cy0 = y > s->clip_y0 ? y : s->clip_y0;
            int cx1 = (x + w) < s->clip_x1 ? (x + w) : s->clip_x1;
            int cy1 = (y + h) < s->clip_y1 ? (y + h) : s->clip_y1;
            if (cx1 > cx0 && cy1 > cy0 && zgl_target_tiles_clean(t0, cx0, cy0, cx1, cy1)) {
                return;
            }
        }
    }
    int full_opaque = (a == 255 && zgl_covers_full_surface(s, x, y, w, h));
    if (full_opaque && g_gl_state > 0) {
        zgl_target *t0 = zgl_target_of(s);
        if (t0) t0->cpu_ahead = 0;
    }
    if (!zgl_begin(s, ZGL_MODE_BLEND)) { cpu_fill_rect(s, x, y, w, h, c); return; }
    zgl_target *t = full_opaque ? zgl_target_of(s) : NULL;
    if (t) t->opaque_dst = 1;
    zgl_rect_quad(s, x, y, w, h, 0, ZAN_CORNERS_ALL, 0, -1, c, c, 0, 0);
    if (t) zgl_target_reset_base_clear(t, c);
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
    zgl_target *t = (s->id >= 0 && s->id < 64) ? &g_zgl_targets[s->id] : zgl_target_of(s);
    int fast_blend = (t && t->opaque_dst);
    if (!fast_blend && g_zgl.count) zgl_flush();
    int kind = fast_blend ? ZGL_K_SURFACE_FAST : ZGL_K_SURFACE;
    float x0 = (float)x, y0 = (float)y;
    float x1 = (float)(x + w), y1 = (float)(y + h);
    float cx0, cy0, cx1, cy1;
    float *v = zgl_alloc_quad(s, kind, &x0, &y0, &x1, &y1, &cx0, &cy0, &cx1, &cy1);
    if (v) {
        float hw = (float)w * 0.5f, hh = (float)h * 0.5f;
        int r = radius < 0 ? 0 : radius;
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        v[0] = x0; v[1] = y0; v[2] = x1; v[3] = y1;
        v[4] = 0.0f; v[5] = 0.0f; v[6] = 0.0f; v[7] = 0.0f;
        v[8] = (float)x + hw; v[9] = (float)y + hh;
        v[10] = hw; v[11] = hh;
        v[12] = (float)r; v[13] = thickness > 0 ? (float)thickness : 0.0f;
        v[14] = (float)kind; v[15] = (float)(corners & 15);
        v[16] = 0.0f; v[17] = 0.0f;
        zgl_color(v + 18, fill, 0);
        zgl_color(v + 22, border, 0);
        v[26] = 0.0f; v[27] = 0.0f; v[28] = 0.0f; v[29] = 0.0f;
        v[30] = cx0; v[31] = cy0; v[32] = cx1; v[33] = cy1;
        v[34] = 0.0f; v[35] = 0.0f; v[36] = 0.0f; v[37] = 0.0f;
    }
    if (!fast_blend) zgl_flush();
}

static void gl_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, u32 top, u32 bottom) {
    /* opaque=1 forces src.a=1.0, so ZGL_MODE_BLEND produces bit-identical output
     * to ZGL_MODE_REPLACE while preserving batching with neighboring primitives. */
    int full_opaque = (radius <= 0 && zgl_covers_full_surface(s, x, y, w, h));
    if (full_opaque && g_gl_state > 0) {
        zgl_target *t0 = zgl_target_of(s);
        if (t0) t0->cpu_ahead = 0;
    }
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_vgrad(s, x, y, w, h, radius, corners, top, bottom); return;
    }
    if (full_opaque) {
        zgl_target *t = zgl_target_of(s);
        if (t) t->opaque_dst = 1;
    }
    zgl_rect_quad(s, x, y, w, h, radius, corners, 0, ZAN_GRAD_VERTICAL,
                  top, bottom, 0, 1);
}

static void gl_fill_grad(zan_surface_t *s, int x, int y, int w, int h,
                         int radius, int corners, int dir,
                         u32 from, u32 via, u32 to) {
    int full_opaque = (radius <= 0
                       && ((from >> 24) & 0xFF) == 255
                       && ((to >> 24) & 0xFF) == 255
                       && (via == 0 || ((via >> 24) & 0xFF) == 255)
                       && zgl_covers_full_surface(s, x, y, w, h));
    if (full_opaque && g_gl_state > 0) {
        zgl_target *t0 = zgl_target_of(s);
        if (t0) t0->cpu_ahead = 0;
    }
    if (!zgl_begin(s, ZGL_MODE_BLEND)) {
        cpu_fill_grad(s, x, y, w, h, radius, corners, dir, from, via, to);
        return;
    }
    if (full_opaque) {
        zgl_target *t = zgl_target_of(s);
        if (t) t->opaque_dst = 1;
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
    g_zgl.st_fbo = t->cov_fbo;
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
    float pad = half + 1.0f;
    float cx0 = fmaxf(0.0f, (float)s->clip_x0);
    float cy0 = fmaxf(0.0f, (float)s->clip_y0);
    float cx1 = fminf((float)s->width, (float)s->clip_x1);
    float cy1 = fminf((float)s->height, (float)s->clip_y1);
    if (cx0 >= cx1 || cy0 >= cy1) return;
    float lo_x = cx1, lo_y = cy1, hi_x = cx0, hi_y = cy0;
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
            float s_min_x = fminf(ax, bx) - pad, s_max_x = fmaxf(ax, bx) + pad;
            float s_min_y = fminf(ay, by) - pad, s_max_y = fmaxf(ay, by) + pad;
            if (s_max_x <= cx0 || s_min_x >= cx1 || s_max_y <= cy0 || s_min_y >= cy1) continue;
            segments++;
            lo_x = fminf(lo_x, s_min_x);
            lo_y = fminf(lo_y, s_min_y);
            hi_x = fmaxf(hi_x, s_max_x);
            hi_y = fmaxf(hi_y, s_max_y);
        }
        off += n * 2;
    }
    lo_x = fmaxf(lo_x, cx0);
    lo_y = fmaxf(lo_y, cy0);
    hi_x = fminf(hi_x, cx1);
    hi_y = fminf(hi_y, cy1);
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
    g_zgl.st_fbo = t->cov_fbo;
    gl.Enable(ZGL_SCISSOR_TEST);
    gl.Scissor(clear_x, t->h - clear_y1, clear_x1 - clear_x, clear_y1 - clear_y);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(ZGL_COLOR_BUFFER_BIT);
    gl.Disable(ZGL_SCISSOR_TEST);
    g_zgl.mode = ZGL_MODE_UNION;
    off = 0;
    const size_t chunk_floats = (size_t)16384 * (size_t)ZGL_IF;
    for (int p = 0; p < n_paths; p++) {
        int n = counts[p];
        for (int i = 0; n >= 2 && i + 1 < n; i++) {
            float ax = (float)pts[off + i * 2] * scale;
            float ay = (float)pts[off + i * 2 + 1] * scale;
            float bx = (float)pts[off + i * 2 + 2] * scale;
            float by = (float)pts[off + i * 2 + 3] * scale;
            float dx = bx - ax, dy = by - ay;
            if (dx * dx + dy * dy < 0.0001f) continue;
            if (fmaxf(ax, bx) + pad <= cx0 || fminf(ax, bx) - pad >= cx1 ||
                fmaxf(ay, by) + pad <= cy0 || fminf(ay, by) - pad >= cy1) continue;
            gl_capsule(s, ax, ay, bx, by, half, 0xFFFFFFFFu);
            if (g_zgl.count >= chunk_floats) zgl_flush();
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
