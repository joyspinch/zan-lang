static void cpu_draw_square_rect(zan_surface_t *s, int x, int y, int w, int h,
                                 u32 c, int t) {
    /* top */ cpu_fill_rect(s, x, y, w, t, c);
    /* bottom */ cpu_fill_rect(s, x, y + h - t, w, t, c);
    /* left */ cpu_fill_rect(s, x, y + t, t, h - 2*t, c);
    /* right */ cpu_fill_rect(s, x + w - t, y + t, t, h - 2*t, c);
}

EXPORT void zan_gui_draw_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_round)->draw_round(s, (int)x, (int)y, (int)w, (int)h,
                                        0, ZAN_CORNERS_ALL, (u32)color,
                                        (int)thickness);
}

#define ZAN_CORNER_TL 1
#define ZAN_CORNER_TR 2
#define ZAN_CORNER_BR 4
#define ZAN_CORNER_BL 8

static void cpu_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int mask, u32 c) {
    int r = radius;
    int ix = x, iy = y, iw = w, ih = h;
    int m = mask;
    if (r <= 0 || (m & 15) == 0) { cpu_fill_rect(s, x, y, w, h, c); return; }
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    ZAN_STAT(g_st_round, (long long)iw * (long long)ih);

    int xl = ix + r, xr = ix + iw - r;
    int yt = iy + r, yb = iy + ih - r;
    cpu_fill_rect(s, xl, iy, iw - 2*r, r, c);
    cpu_fill_rect(s, ix, yt, iw, ih - 2*r, c);      /* middle */
    cpu_fill_rect(s, xl, yb, iw - 2*r, r, c);

    double cc[4][2] = {
        {xl, yt},     /* TL, square px<xl, py<yt */
        {xr, yt},     /* TR, square px>=xr, py<yt */
        {xl, yb},     /* BL, square px<xl, py>=yb */
        {xr, yb}      /* BR, square px>=xr, py>=yb */
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            cpu_fill_rect(s, zx[ci], zy[ci], r, r, c);
            continue;
        }
        double ccx = cc[ci][0], ccy = cc[ci][1];
        for (int py = zy[ci]; py < zy[ci] + r; py++) {
            for (int px = zx[ci]; px < zx[ci] + r; px++) {
                double ddx = (double)px + 0.5 - ccx;
                double ddy = (double)py + 0.5 - ccy;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double cov = (double)r - dist + 0.5;
                if (cov >= 1.0) set_pixel(s, px, py, c);
                else if (cov > 0.0) set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

EXPORT void zan_gui_fill_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_round)->fill_round(s, (int)x, (int)y, (int)w, (int)h,
                                        (int)radius, (int)mask, (u32)color);
}

EXPORT void zan_gui_fill_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color) {
    zan_gui_fill_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color);
}

static u32 surface_round_color(u32 fill, u32 border, int outer, int inner) {
    uint64_t af = fill >> 24, ab = border >> 24;
    uint64_t bw = ab * (uint64_t)(outer - inner);
    uint64_t fw = af * ((uint64_t)outer * 255u - bw);
    bw *= 255u;
    uint64_t weight = fw + bw;
    if (!weight) return 0;
    u32 alpha = (u32)(weight / (255u * 255u));
    u32 rgb = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint64_t c = ((fill >> shift) & 255u) * fw
                   + ((border >> shift) & 255u) * bw;
        rgb |= (u32)(c / weight) << shift;
    }
    return (alpha << 24) | rgb;
}

static void cpu_surface_round(zan_surface_t *s, int x, int y, int w, int h,
                              int radius, int mask, u32 fill, u32 border,
                              int thickness) {
    if (w <= 0 || h <= 0) return;
    if (thickness <= 0 || (border >> 24) == 0) {
        cpu_fill_round(s, x, y, w, h, radius, mask, fill);
        return;
    }
    int r = clamp_i(radius, 0, (w < h ? w : h) / 2);
    int t = thickness < (w < h ? w : h) ? thickness : (w < h ? w : h);
    int iw = w - t - t, ih = h - t - t, ir = r > t ? r - t : 0;
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = (int)((int64_t)x + w < s->clip_x1 ? (int64_t)x + w : s->clip_x1);
    int y1 = (int)((int64_t)y + h < s->clip_y1 ? (int64_t)y + h : s->clip_y1);
    if (x1 <= x0 || y1 <= y0) return;
    ZAN_STAT(g_st_round, (long long)(x1-x0) * (y1-y0));
    for (int py = y0; py < y1; py++) {
        u32 *row = s->pixels + py * s->stride;
        for (int px = x0; px < x1; px++) {
            int i = px-x, j = py-y;
            int outer = zan_round_cov(i, j, w, h, r, mask);
            if (!outer) continue;
            int inner = 0;
            if (iw > 0 && ih > 0 && i >= t && j >= t && i-t < iw && j-t < ih)
                inner = zan_round_cov(i-t, j-t, iw, ih, ir, mask);
            if (inner > outer) inner = outer;
            u32 c = inner == outer ? ((fill & 0xFFFFFFu) |
                    ((u32)((fill >> 24) * outer / 255) << 24))
                    : surface_round_color(fill, border, outer, inner);
            row[px] = blend_over(row[px], c);
        }
    }
}

EXPORT void zan_gui_surface_rounded_rect_mask(i32 surface_id, i32 x, i32 y,
        i32 w, i32 h, i32 radius, i32 mask, i32 fill, i32 border, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || w <= 0 || h <= 0) return;
    ZAN_IMPL(s, surface_round)->surface_round(s, x, y, w, h, radius, mask,
                                             (u32)fill, (u32)border, thickness);
}

EXPORT void zan_gui_surface_rounded_rect(i32 surface_id, i32 x, i32 y,
        i32 w, i32 h, i32 radius, i32 fill, i32 border, i32 thickness) {
    zan_gui_surface_rounded_rect_mask(surface_id, x, y, w, h, radius, 15,
                                    fill, border, thickness);
}

/* 抗锯齿圆角矩形描边光栅化 */
static void cpu_draw_round(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int mask, u32 c, int thickness) {
    int ix = x, iy = y, iw = w, ih = h;
    int r = radius, th = thickness;
    int m = mask;
    if (r <= 0 || (m & 15) == 0) {
        cpu_draw_square_rect(s, x, y, w, h, c, thickness);
        return;
    }
    if (th < 1) th = 1;
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    if (th > r) th = r;

    int xl = ix + r, xr = ix + iw - r;
    int yt = iy + r, yb = iy + ih - r;
    cpu_fill_rect(s, xl, iy, iw - 2*r, th, c);              /* top */
    cpu_fill_rect(s, xl, iy + ih - th, iw - 2*r, th, c);    /* bottom */
    cpu_fill_rect(s, ix, yt, th, ih - 2*r, c);             /* left */
    cpu_fill_rect(s, ix + iw - th, yt, th, ih - 2*r, c);   /* right */

    double cc[4][2] = {
        {xl, yt}, {xr, yt}, {xl, yb}, {xr, yb}
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    int hx[4] = {ix, xr, ix, xr};
    double rin = (double)r - (double)th;
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            int cy = (ci < 2) ? zy[ci] : zy[ci] + r - th;
            int cx = (ci % 2 == 0) ? hx[ci] : hx[ci] + r - th;
            cpu_fill_rect(s, zx[ci], cy, r, th, c);
            cpu_fill_rect(s, cx, zy[ci], th, r, c);
            continue;
        }
        double ccx = cc[ci][0], ccy = cc[ci][1];
        for (int py = zy[ci]; py < zy[ci] + r; py++) {
            for (int px = zx[ci]; px < zx[ci] + r; px++) {
                double ddx = (double)px + 0.5 - ccx;
                double ddy = (double)py + 0.5 - ccy;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double outerCov = (double)r + 0.5 - dist;
                double innerCov = dist - (rin - 0.5);
                double cov = outerCov < innerCov ? outerCov : innerCov;
                if (cov <= 0.0) continue;
                if (cov >= 1.0) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

EXPORT void zan_gui_draw_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_round)->draw_round(s, (int)x, (int)y, (int)w, (int)h,
                                        (int)radius, (int)mask, (u32)color,
                                        (int)thickness);
}

EXPORT void zan_gui_draw_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color, i32 thickness) {
    zan_gui_draw_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color, thickness);
}

static void cpu_shadow_round(zan_surface_t *s, int x, int y, int w, int h,
                             int radius, int blur, u32 c) {
    int iw = w, ih = h;
    if (iw <= 0 || ih <= 0) return;
    int bl = blur;
    if (bl <= 0) {
        cpu_fill_round(s, x, y, w, h, radius, ZAN_CORNERS_ALL, c);
        return;
    }
    int r = radius;
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    if (r < 0) r = 0;

    double cx = (double)x + (double)iw / 2.0;
    double cy = (double)y + (double)ih / 2.0;
    double ex = (double)iw / 2.0 - (double)r;
    double ey = (double)ih / 2.0 - (double)r;

    int x0 = x - bl, x1 = x + iw + bl;
    int y0 = y - bl, y1 = y + ih + bl;
    if (x0 < s->clip_x0) x0 = s->clip_x0;
    if (y0 < s->clip_y0) y0 = s->clip_y0;
    if (x1 > s->clip_x1) x1 = s->clip_x1;
    if (y1 > s->clip_y1) y1 = s->clip_y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->width) x1 = s->width;
    if (y1 > s->height) y1 = s->height;

    int in = r + (bl + 1) / 2 + 1;
    int xi0 = x + in, xi1 = x + iw - in;
    int yi0 = y + in, yi1 = y + ih - in;
    if (xi1 > xi0 && yi1 > yi0) {
        cpu_fill_rect(s, xi0, yi0, xi1 - xi0, yi1 - yi0, c);
    } else {
        xi0 = 0; xi1 = 0; yi0 = 0; yi1 = 0;
    }

    double half = (double)bl / 2.0;
    double inv = 1.0 / (double)bl;
    for (int py = y0; py < y1; py++) {
        double qy = fabs((double)py + 0.5 - cy) - ey;
        int interiorRow = (py >= yi0 && py < yi1);
        for (int px = x0; px < x1; px++) {
            if (interiorRow && px >= xi0 && px < xi1) { px = xi1 - 1; continue; }
            double qx = fabs((double)px + 0.5 - cx) - ex;
            double mx = qx > 0.0 ? qx : 0.0;
            double my = qy > 0.0 ? qy : 0.0;
            double outside = sqrt(mx*mx + my*my);
            double inside = (qx > qy ? qx : qy);
            if (inside > 0.0) inside = 0.0;
            double d = outside + inside - (double)r;
            double t = (d + half) * inv;
            if (t <= 0.0) { set_pixel(s, px, py, c); continue; }
            if (t >= 1.0) continue;
            double cov = 1.0 - t * t * (3.0 - 2.0 * t);
            set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
        }
    }
}

EXPORT void zan_gui_shadow_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 blur, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, shadow_round)->shadow_round(s, (int)x, (int)y, (int)w, (int)h,
                                            (int)radius, (int)blur,
                                            (u32)color);
}

/* 抗锯齿填充圆光栅化（亚像素采样） */
static void cpu_fill_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c) {
    int r = radius;
    double rr = (double)r;
    double rin = rr - 0.5, rout = rr + 0.5;
    double rin2 = rin * rin, rout2 = rout * rout;

    ZAN_STAT(g_st_round, (long long)(2 * r + 3) * (long long)(2 * r + 3));
    int ext = r + 2;
    int ylo = cy - ext, yhi = cy + ext;
    if (ylo < s->clip_y0) ylo = s->clip_y0;
    if (yhi >= s->clip_y1) yhi = s->clip_y1 - 1;
    for (int py = ylo; py <= yhi; py++) {
        double wy = (double)py + 0.5 - (double)cy;
        double wy2 = wy * wy;
        if (wy2 > rout2) continue;
        u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
        double hs = (rin2 > wy2) ? sqrt(rin2 - wy2) : -1.0;
        int L = cx + (int)ceil(-hs - 0.5);
        int Rr = cx + (int)floor(hs - 0.5);
        if (L < cx - ext) L = cx - ext;
        if (Rr > cx + ext) Rr = cx + ext;
        int xlo = cx - ext, xhi = cx + ext;
        if (xlo < s->clip_x0) xlo = s->clip_x0;
        if (xhi >= s->clip_x1) xhi = s->clip_x1 - 1;
        for (int px = xlo; px <= xhi; px++) {
            if (px >= L && px <= Rr) {
                row[px] = blend_over(row[px], c);
                continue;
            }
            double wx = (double)px + 0.5 - (double)cx;
            double dist = sqrt(wx * wx + wy2);
            double cov = rout - dist;
            if (cov <= 0.0) continue;
            set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
        }
    }
}

EXPORT void zan_gui_fill_circle(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_circle)->fill_circle(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color);
}

static void cpu_draw_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c, int thickness) {
    int r = radius;
    if (r <= 0) return;
    double half = (double)(thickness > 0 ? thickness : 1) / 2.0;
    int ext = r + (int)half + 2;

    for (int py = cy - ext; py <= cy + ext; py++) {
        if (py < s->clip_y0 || py >= s->clip_y1) continue;
        double wy = (double)py + 0.5 - (double)cy;
        for (int px = cx - ext; px <= cx + ext; px++) {
            if (px < s->clip_x0 || px >= s->clip_x1) continue;
            double wx = (double)px + 0.5 - (double)cx;
            double d = fabs(sqrt(wx * wx + wy * wy) - (double)r);
            if (d <= half - 0.5) {
                set_pixel(s, px, py, c);
            } else if (d <= half + 0.5) {
                int cov = (int)((half + 0.5 - d) * 255.0);
                set_pixel_aa(s, px, py, c, cov);
            }
        }
    }
}

EXPORT void zan_gui_draw_circle(
    i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_circle)->draw_circle(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color, (int)thickness);
}

static void cpu_fill_radial(zan_surface_t *s, int cx, int cy, int radius,
                            u32 color, int inner_a) {
    int r = radius;
    if (r <= 0) return;
    int icx = cx, icy = cy;
    u32 rgb = color & 0x00FFFFFFu;
    int peak = inner_a;
    if (peak > 255) peak = 255;
    if (peak <= 0) return;
    int r2 = r * r;
    ZAN_STAT(g_st_radial, (long long)(2 * r + 1) * (long long)(2 * r + 1));
    int y_lo = icy - r, y_hi = icy + r;
    if (y_lo < s->clip_y0) y_lo = s->clip_y0;
    if (y_hi > s->clip_y1 - 1) y_hi = s->clip_y1 - 1;
    for (int py = y_lo; py <= y_hi; py++) {
        int dy = py - icy;
        int dy2 = dy * dy;
        int span = r2 - dy2;
        if (span <= 0) continue;
        int dxMax = (int)sqrt((double)span);
        while (dxMax > 0 && dxMax * dxMax >= span) dxMax--;
        while ((dxMax + 1) * (dxMax + 1) < span) dxMax++;
        int x_lo = icx - dxMax, x_hi = icx + dxMax;
        if (x_lo < s->clip_x0) x_lo = s->clip_x0;
        if (x_hi > s->clip_x1 - 1) x_hi = s->clip_x1 - 1;
        u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
        for (int px = x_lo; px <= x_hi; px++) {
            int dx = px - icx;
            int d2 = dx * dx + dy2;
            int t = ((r2 - d2) << 8) / r2;
            int a = (peak * t * t) >> 16;
            if (a <= 0) continue;
            row[px] = blend_over(row[px], ((u32)a << 24) | rgb);
        }
    }
}

EXPORT void zan_gui_fill_radial(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 inner_a) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_radial)->fill_radial(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color, (int)inner_a);
}

EXPORT void zan_gui_fill_rects(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 5;
        ZAN_IMPL(s, fill_rect)->fill_rect(s, (int)d[0], (int)d[1], (int)d[2],
                                          (int)d[3], (u32)d[4]);
    }
}

EXPORT void zan_gui_fill_circles(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 4;
        ZAN_IMPL(s, fill_circle)->fill_circle(s, (int)d[0], (int)d[1],
                                              (int)d[2], (u32)d[3]);
    }
}

EXPORT void zan_gui_fill_radials(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 5;
        ZAN_IMPL(s, fill_radial)->fill_radial(s, (int)d[0], (int)d[1],
                                              (int)d[2], (u32)d[3], (int)d[4]);
    }
}

EXPORT void zan_gui_blit_surface(i32 dst_id, i32 src_id, i32 dst_x, i32 dst_y,
                                 i32 src_x, i32 src_y, i32 w, i32 h) {
    if (dst_id < 0 || dst_id >= g_surface_count) return;
    if (src_id < 0 || src_id >= g_surface_count) return;
    zan_surface_t *d = g_surfaces[dst_id];
    zan_surface_t *s = g_surfaces[src_id];
    if (!d || !s || !d->pixels || !s->pixels) return;
    if (w <= 0 || h <= 0) return;
    if (s->be && s->be->sync_to_cpu) s->be->sync_to_cpu(s);
    if (d->be && d->be->sync_to_cpu) d->be->sync_to_cpu(d);
    int sx = (int)src_x, sy = (int)src_y, dx = (int)dst_x, dy = (int)dst_y;
    int cw = (int)w, ch = (int)h;
    if (sx < 0) { dx -= sx; cw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; ch += sy; sy = 0; }
    if (sx + cw > s->width) cw = s->width - sx;
    if (sy + ch > s->height) ch = s->height - sy;
    if (dx < d->clip_x0) { int cut = d->clip_x0 - dx; dx += cut; sx += cut; cw -= cut; }
    if (dy < d->clip_y0) { int cut = d->clip_y0 - dy; dy += cut; sy += cut; ch -= cut; }
    if (dx + cw > d->clip_x1) cw = d->clip_x1 - dx;
    if (dy + ch > d->clip_y1) ch = d->clip_y1 - dy;
    if (cw <= 0 || ch <= 0) return;
    for (int row = 0; row < ch; row++) {
        u32 *from = s->pixels + (size_t)(sy + row) * (size_t)s->stride + (size_t)sx;
        u32 *to = d->pixels + (size_t)(dy + row) * (size_t)d->stride + (size_t)dx;
        memcpy(to, from, (size_t)cw * sizeof(u32));
    }
    if (d->be && d->be->sync_from_cpu) d->be->sync_from_cpu(d);
}

static void cpu_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                            int r_outer, int a0_deg, int a1_deg, u32 c) {
    if (r_outer <= 0) return;
    double a0 = (double)a0_deg, a1 = (double)a1_deg;
    if (a1 < a0) { double tmp = a0; a0 = a1; a1 = tmp; }
    double sweep = a1 - a0;
    if (sweep <= 0.0) return;
    if (sweep > 360.0) sweep = 360.0;
    a0 = a0 - floor(a0 / 360.0) * 360.0;
    a1 = a0 + sweep;

    const double PI = 3.14159265358979323846;
    double ri = (double)r_inner;
    if (ri < 0.0) ri = 0.0;
    double ro = (double)r_outer;
    int hasHole = ri > 0.0;
    double roOut = ro + 0.5, roIn = ro - 0.5;
    double riOut = ri - 0.5, riIn = ri + 0.5;
    if (riOut < 0.0) riOut = 0.0;
    double roOut2 = roOut * roOut, roIn2 = roIn * roIn;
    double riOut2 = riOut * riOut, riIn2 = riIn * riIn;
    if (roIn2 < 0.0) roIn2 = 0.0;
    int full = sweep >= 360.0;

    double e0x = sin(a0 * PI / 180.0), e0y = -cos(a0 * PI / 180.0);
    double e1x = sin(a1 * PI / 180.0), e1y = -cos(a1 * PI / 180.0);
    /* cross(v,w) = v */
    int wide = sweep > 180.0;

    int bx0 = cx - r_outer - 1, bx1 = cx + r_outer + 1;
    int by0 = cy - r_outer - 1, by1 = cy + r_outer + 1;
    if (!full) {
        double xmn = e0x * ri, xmx = xmn, ymn = e0y * ri, ymx = ymn;
        double cand[10][2];
        int n = 0;
        cand[n][0] = e0x * ro; cand[n][1] = e0y * ro; n++;
        cand[n][0] = e1x * ri; cand[n][1] = e1y * ri; n++;
        cand[n][0] = e1x * ro; cand[n][1] = e1y * ro; n++;
        if (!hasHole) { cand[n][0] = 0.0; cand[n][1] = 0.0; n++; }
        for (int k = 0; k <= 8; k++) {
            double ca = (double)(k * 90);
            if (ca < a0 || ca > a1) continue;
            cand[n][0] = sin(ca * PI / 180.0) * ro;
            cand[n][1] = -cos(ca * PI / 180.0) * ro;
            n++;
        }
        for (int i = 0; i < n; i++) {
            if (cand[i][0] < xmn) xmn = cand[i][0];
            if (cand[i][0] > xmx) xmx = cand[i][0];
            if (cand[i][1] < ymn) ymn = cand[i][1];
            if (cand[i][1] > ymx) ymx = cand[i][1];
        }
        int nx0 = cx + (int)floor(xmn) - 1, nx1 = cx + (int)ceil(xmx) + 1;
        int ny0 = cy + (int)floor(ymn) - 1, ny1 = cy + (int)ceil(ymx) + 1;
        if (nx0 > bx0) bx0 = nx0;
        if (nx1 < bx1) bx1 = nx1;
        if (ny0 > by0) by0 = ny0;
        if (ny1 < by1) by1 = ny1;
    }
    if (bx0 < s->clip_x0) bx0 = s->clip_x0;
    if (by0 < s->clip_y0) by0 = s->clip_y0;
    if (bx1 > s->clip_x1 - 1) bx1 = s->clip_x1 - 1;
    if (by1 > s->clip_y1 - 1) by1 = s->clip_y1 - 1;

    for (int py = by0; py <= by1; py++) {
        double wy = (double)py + 0.5 - (double)cy;
        double wy2 = wy * wy;
        if (wy2 > roOut2) continue;
        double half = sqrt(roOut2 - wy2);
        int rx0 = (int)ceil((double)cx - half - 0.5);
        int rx1 = (int)floor((double)cx + half - 0.5);
        if (rx0 < bx0) rx0 = bx0;
        if (rx1 > bx1) rx1 = bx1;
        int spans[2][2];
        int nspan = 0;
        if (hasHole && wy2 < riOut2) {
            double hi = sqrt(riOut2 - wy2);
            int hx0 = (int)floor((double)cx - hi - 0.5);
            int hx1 = (int)ceil((double)cx + hi - 0.5);
            spans[nspan][0] = rx0; spans[nspan][1] = hx0 < rx1 ? hx0 : rx1;
            nspan++;
            spans[nspan][0] = hx1 > rx0 ? hx1 : rx0; spans[nspan][1] = rx1;
            nspan++;
        } else {
            spans[nspan][0] = rx0; spans[nspan][1] = rx1;
            nspan++;
        }
        for (int sp = 0; sp < nspan; sp++) {
            int x0 = spans[sp][0], x1 = spans[sp][1];
            if (x0 < bx0) x0 = bx0;
            if (x1 > bx1) x1 = bx1;
            for (int px = x0; px <= x1; px++) {
                double wx = (double)px + 0.5 - (double)cx;
                double d2 = wx * wx + wy2;
                if (d2 > roOut2) continue;
                if (hasHole && d2 < riOut2) continue;
                double radCov = 1.0;
                if (d2 > roIn2) radCov = roOut - sqrt(d2);
                else if (hasHole && d2 < riIn2) radCov = sqrt(d2) - riOut;
                if (radCov <= 0.0) continue;
                if (radCov > 1.0) radCov = 1.0;
                double angCov = 1.0;
                if (!full) {
                    if (wide) {
                        double gLo = e1x * wy - e1y * wx;
                        double gHi = wx * e0y - wy * e0x;
                        double covLo = gLo + 0.5, covHi = gHi + 0.5;
                        if (covLo > 1.0) covLo = 1.0;
                        if (covLo < 0.0) covLo = 0.0;
                        if (covHi > 1.0) covHi = 1.0;
                        if (covHi < 0.0) covHi = 0.0;
                        angCov = 1.0 - covLo * covHi;
                    } else {
                        double covLo = (e0x * wy - e0y * wx) + 0.5;
                        double covHi = (wx * e1y - wy * e1x) + 0.5;
                        if (covLo <= 0.0 || covHi <= 0.0) continue;
                        if (covLo > 1.0) covLo = 1.0;
                        if (covHi > 1.0) covHi = 1.0;
                        angCov = covLo * covHi;
                    }
                    if (angCov <= 0.0) continue;
                }
                int cov = (int)(radCov * angCov * 255.0);
                if (cov >= 255) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, cov);
            }
        }
    }
}

EXPORT void zan_gui_fill_sector(
    i32 surface_id, i32 cx, i32 cy, i32 r_inner, i32 r_outer, i32 a0_deg,
    i32 a1_deg, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_sector)->fill_sector(s, (int)cx, (int)cy, (int)r_inner,
                                         (int)r_outer, (int)a0_deg,
                                         (int)a1_deg, (u32)color);
}

/* 吴小林抗锯齿直线光栅化算法 */
static void cpu_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                          u32 c, int thickness) {
    int t = thickness;

    if (t <= 1) {
        int steep = abs(y1 - y0) > abs(x1 - x0);
        double fx0 = (double)x0, fy0 = (double)y0, fx1 = (double)x1, fy1 = (double)y1;
        if (steep) { double tmp; tmp = fx0; fx0 = fy0; fy0 = tmp; tmp = fx1; fx1 = fy1; fy1 = tmp; }
        if (fx0 > fx1) { double tmp; tmp = fx0; fx0 = fx1; fx1 = tmp; tmp = fy0; fy0 = fy1; fy1 = tmp; }
        double dx = fx1 - fx0, dy = fy1 - fy0;
        double gradient = (dx == 0) ? 1.0 : dy / dx;
        double intery = fy0 + gradient;

        int xpxl1 = (int)fx0, xpxl2 = (int)fx1;
        if (steep) {
            set_pixel(s, (int)fy0, xpxl1, c);
            set_pixel(s, (int)fy1, xpxl2, c);
        } else {
            set_pixel(s, xpxl1, (int)fy0, c);
            set_pixel(s, xpxl2, (int)fy1, c);
        }

        for (int xi = xpxl1 + 1; xi < xpxl2; xi++) {
            int iy = (int)intery;
            double fpart = intery - (double)iy;
            int c1 = (int)((1.0 - fpart) * 255);
            int c2 = (int)(fpart * 255);
            if (steep) {
                set_pixel_aa(s, iy, xi, c, c1);
                set_pixel_aa(s, iy + 1, xi, c, c2);
            } else {
                set_pixel_aa(s, xi, iy, c, c1);
                set_pixel_aa(s, xi, iy + 1, c, c2);
            }
            intery += gradient;
        }
    } else {
        /* 宽线圆头抗锯齿几何光栅化 */
        double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
        double len2 = dx*dx + dy*dy;
        double half = t * 0.5;
        int lox = (x0 < x1 ? x0 : x1);
        int hix = (x0 > x1 ? x0 : x1);
        int loy = (y0 < y1 ? y0 : y1);
        int hiy = (y0 > y1 ? y0 : y1);
        int minx = lox - t - 1, maxx = hix + t + 1;
        int miny = loy - t - 1, maxy = hiy + t + 1;
        double ux = 0.0, uy = 0.0, seglen = sqrt(len2);
        if (seglen > 0.0001) { ux = dx / seglen; uy = dy / seglen; }
        double rr = half + 0.5;
        if (miny < s->clip_y0) miny = s->clip_y0;
        if (maxy > s->clip_y1 - 1) maxy = s->clip_y1 - 1;
        if (minx < s->clip_x0) minx = s->clip_x0;
        if (maxx > s->clip_x1 - 1) maxx = s->clip_x1 - 1;
        for (int py = miny; py <= maxy; py++) {
            double yc = (double)py + 0.5;
            double lo = 1e30, hi = -1e30;
            for (int e = 0; e < 2; e++) {
                double ecx = e ? (double)x1 : (double)x0;
                double ecy = e ? (double)y1 : (double)y0;
                double dyc = yc - ecy;
                if (dyc * dyc > rr * rr) continue;
                double half_w = sqrt(rr * rr - dyc * dyc);
                if (ecx - half_w < lo) lo = ecx - half_w;
                if (ecx + half_w > hi) hi = ecx + half_w;
            }
            if (seglen > 0.0001) {
                double nx = -uy * rr, ny = ux * rr;
                for (int e = 0; e < 2; e++) {
                    double sgn = e ? -1.0 : 1.0;
                    double ax = (double)x0 + sgn * nx, ay = (double)y0 + sgn * ny;
                    double bx = (double)x1 + sgn * nx, by = (double)y1 + sgn * ny;
                    double span = by - ay;
                    if (span > -0.0001 && span < 0.0001) continue;
                    double tt = (yc - ay) / span;
                    if (tt < 0.0 || tt > 1.0) continue;
                    double xs = ax + (bx - ax) * tt;
                    if (xs < lo) lo = xs;
                    if (xs > hi) hi = xs;
                }
            }
            if (lo > hi) continue;
            int rowLo = (int)floor(lo) - 1, rowHi = (int)ceil(hi) + 1;
            if (rowLo < minx) rowLo = minx;
            if (rowHi > maxx) rowHi = maxx;
            for (int px = rowLo; px <= rowHi; px++) {
                double pxc = (double)px + 0.5, pyc = (double)py + 0.5;
                double proj = 0.0;
                if (len2 > 0.0001) {
                    proj = ((pxc - (double)x0) * dx + (pyc - (double)y0) * dy) / len2;
                    if (proj < 0.0) proj = 0.0;
                    if (proj > 1.0) proj = 1.0;
                }
                double cxp = (double)x0 + proj * dx;
                double cyp = (double)y0 + proj * dy;
                double ddx = pxc - cxp, ddy = pyc - cyp;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double cov = half + 0.5 - dist;
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                if (icov >= 255) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, icov);
            }
        }
    }
}

EXPORT void zan_gui_draw_line(i32 surface_id, i32 x0, i32 y0, i32 x1, i32 y1, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_line)->draw_line(s, (int)x0, (int)y0, (int)x1, (int)y1,
                                     (u32)color, (int)thickness);
}

static u8  *g_poly_cov = NULL;
static i32  g_poly_cov_w = 0;
static i32  g_poly_cov_h = 0;
static i32 *g_poly_dirty = NULL;
static i32  g_poly_dirty_n = 0;
static i32  g_poly_dirty_cap = 0;

/* 连续折线批处理光栅化 */
static void zan_polyline_core(zan_surface_t *s, const i32 *pts, i32 n,
                              u32 c, int t, int shift) {
    int W = s->width, H = s->height;
    if (W <= 0 || H <= 0) return;
    double scale = shift == 0 ? 1.0 : (1.0 / 256.0);

    if (g_poly_cov_w < W || g_poly_cov_h < H) {
        free(g_poly_cov);
        g_poly_cov = (u8*)malloc((size_t)W * (size_t)H);
        g_poly_cov_w = W;
        g_poly_cov_h = H;
        if (g_poly_cov) memset(g_poly_cov, 0, (size_t)W * (size_t)H);
    }
    if (!g_poly_cov) { g_poly_cov_w = 0; g_poly_cov_h = 0; return; }
    g_poly_dirty_n = 0;

    double half = t * 0.5;
    for (i32 i = 0; i + 1 < n; i++) {
        double x0 = pts[i * 2] * scale, y0 = pts[i * 2 + 1] * scale;
        double x1 = pts[i * 2 + 2] * scale, y1 = pts[i * 2 + 3] * scale;
        if (x0 == x1 && y0 == y1) continue;
        double dx = x1 - x0, dy = y1 - y0;
        double len2 = dx * dx + dy * dy;
        if (len2 < 0.0001) continue;
        int minx = (int)floor(x0 < x1 ? x0 : x1) - t - 2;
        int maxx = (int)ceil(x0 > x1 ? x0 : x1) + t + 2;
        int miny = (int)floor(y0 < y1 ? y0 : y1) - t - 2;
        int maxy = (int)ceil(y0 > y1 ? y0 : y1) + t + 2;
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx >= W) maxx = W - 1;
        if (maxy >= H) maxy = H - 1;
        for (int py = miny; py <= maxy; py++) {
            double pyc = (double)py + 0.5;
            for (int px = minx; px <= maxx; px++) {
                double pxc = (double)px + 0.5;
                double proj = ((pxc - x0) * dx + (pyc - y0) * dy) / len2;
                if (proj < 0.0) proj = 0.0;
                if (proj > 1.0) proj = 1.0;
                double cxp = x0 + proj * dx;
                double cyp = y0 + proj * dy;
                double ddx = pxc - cxp, ddy = pyc - cyp;
                double dist = sqrt(ddx * ddx + ddy * ddy);
                double cov = half + 0.5 - dist;
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                int idx = py * W + px;
                if (g_poly_cov[idx] == 0) {
                    if (g_poly_dirty_n >= g_poly_dirty_cap) {
                        i32 cap2 = g_poly_dirty_cap ? g_poly_dirty_cap * 2 : 1024;
                        i32 *nd = (i32*)realloc(g_poly_dirty,
                            (size_t)cap2 * sizeof(i32));
                        if (!nd) {
                            memset(g_poly_cov, 0, (size_t)W * (size_t)H);
                            g_poly_dirty_n = 0;
                            return;
                        }
                        g_poly_dirty = nd;
                        g_poly_dirty_cap = cap2;
                    }
                    g_poly_dirty[g_poly_dirty_n++] = (py << 16) | (px & 0xFFFF);
                }
                if (icov > g_poly_cov[idx]) g_poly_cov[idx] = (u8)icov;
            }
        }
    }

    for (i32 d = 0; d < g_poly_dirty_n; d++) {
        int px = g_poly_dirty[d] & 0xFFFF;
        int py = (g_poly_dirty[d] >> 16) & 0xFFFF;
        int idx = py * W + px;
        int cov = g_poly_cov[idx];
        g_poly_cov[idx] = 0;
        if (cov >= 255) set_pixel(s, px, py, c);
        else set_pixel_aa(s, px, py, c, cov);
    }
}

EXPORT void zan_gui_draw_polyline(i32 surface_id, const i32 *pts, i32 n,
                                  i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || n < 2) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 0);
}

EXPORT void zan_gui_draw_polyline_fx(i32 surface_id, const i32 *pts, i32 n,
                                     i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || n < 2) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 8);
}

static void cpu_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                         int t, int shift) {
    zan_polyline_core(s, pts, (i32)n, c, t, shift);
}

EXPORT void zan_gui_draw_polybatch(i32 surface_id, const i32 *pts,
                                   const i32 *counts, i32 n_paths,
                                   i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || !counts || n_paths < 1) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    if (s->be && s->be->polybatch) {
        s->be->polybatch(s, pts, counts, (int)n_paths, (u32)color, t, 0);
        return;
    }
    i32 off = 0;
    if (s->be && s->be->polyline) {
        for (i32 p = 0; p < n_paths; p++) {
            i32 n = counts[p];
            if (n >= 2) s->be->polyline(s, pts + off, (int)n, (u32)color, t, 0);
            off += n * 2;
        }
        return;
    }
    for (i32 p = 0; p < n_paths; p++) {
        i32 n = counts[p];
        if (n >= 2) cpu_polyline(s, pts + off, (int)n, (u32)color, t, 0);
        off += n * 2;
    }
}
