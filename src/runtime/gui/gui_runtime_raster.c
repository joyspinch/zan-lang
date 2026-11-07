/* gui_runtime_raster.c -- CPU 软件光栅化、模糊滤镜、渐变解算器与画板快照恢复 */

static void cpu_clear_rect(zan_surface_t *s, int x, int y, int w, int h,
                           u32 c) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
    if (area <= 0) return;
    ZAN_STAT(g_st_fill_op, area);
    stat_top_rect(1, (int)area, x0, y0, x1 - x0, y1 - y0, c);
    for (int py = y0; py < y1; py++) {
        fill_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
    }
}

EXPORT void zan_gui_clear_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, clear_rect)->clear_rect(s, (int)x, (int)y, (int)w, (int)h,
                                        (u32)color);
}

static void cpu_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    u32 sa = (c >> 24) & 0xFF;
    long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
    if (area < 0) area = 0;
    if (sa == 255) {
        ZAN_STAT(g_st_fill_op, area);
        stat_top_rect(1, (int)area, x0, y0, x1 - x0, y1 - y0, c);
        for (int py = y0; py < y1; py++) {
            fill_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
        }
    } else if (sa != 0) {
        ZAN_STAT(g_st_fill_blend, area);
        stat_top_rect(0, (int)area, x0, y0, x1 - x0, y1 - y0, c);
        for (int py = y0; py < y1; py++) {
            blend_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
        }
    }
}

EXPORT void zan_gui_fill_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_rect)->fill_rect(s, (int)x, (int)y, (int)w, (int)h,
                                      (u32)color);
}

static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask);

static void cpu_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int corners, u32 ct, u32 cb) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    int r = radius;
    int m = corners;
    if (r < 0) r = 0;
    if (r > 0 && (m & 15) == 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int rh2 = h;
    if (rh2 < 1) rh2 = 1;
    int denom = rh2 > 1 ? rh2 - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)rw * (long long)rh);
    int tr = (ct >> 16) & 0xFF, tg = (ct >> 8) & 0xFF, tb = ct & 0xFF;
    int mr = (cb >> 16) & 0xFF, mg = (cb >> 8) & 0xFF, mb = cb & 0xFF;
    for (int py = y0; py < y1; py++) {
        int num = py - y;
        if (num < 0) num = 0;
        if (num > rh2 - 1) num = rh2 - 1;
        int rr = tr + (mr - tr) * num / denom;
        int gg = tg + (mg - tg) * num / denom;
        int bb = tb + (mb - tb) * num / denom;
        u32 c = 0xFF000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
        u32 *row = s->pixels + py * s->stride;
        if (r <= 0) {
            for (int px = x0; px < x1; px++) row[px] = c;
            continue;
        }
        for (int px = x0; px < x1; px++) {
            int cov = zan_round_cov(px - x, py - y, w, h, r, m);
            if (cov >= 255) row[px] = c;
            else if (cov > 0) set_pixel_aa(s, px, py, c, cov);
        }
    }
}

EXPORT void zan_gui_fill_vgrad(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color_top,
    i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_vgrad)->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h,
                                       0, ZAN_CORNERS_ALL,
                                       (u32)color_top, (u32)color_bottom);
}

EXPORT void zan_gui_fill_vgrad_mask(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask,
    i32 color_top, i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_vgrad)->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h,
                                       (int)radius, (int)mask,
                                       (u32)color_top, (u32)color_bottom);
}

static u32 grad_sample(u32 from, u32 via, u32 to, int t) {
    u32 a = from, b = to;
    if (via != 0) {
        if (t < 500) { b = via; t = t * 2; }
        else { a = via; t = (t - 500) * 2; }
    }
    int out = 0;
    for (int sh = 0; sh <= 24; sh += 8) {
        int ca = (int)((a >> sh) & 0xFF);
        int cb = (int)((b >> sh) & 0xFF);
        out |= (ca + (cb - ca) * t / 1000) << sh;
    }
    return (u32)out;
}

static void cpu_fill_grad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, int dir,
                          u32 cf, u32 cv, u32 ct) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    if (x1 <= x0 || y1 <= y0) return;
    int r = radius;
    int m = corners;
    if (r < 0) r = 0;
    if (r > 0 && (m & 15) == 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int d = dir;
    int span = (d == 1) ? w : h;
    if (d == 2 || d == 3) span = w + h - 1;
    if (span < 1) span = 1;
    int denom = span > 1 ? span - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)(x1 - x0) * (long long)(y1 - y0));
    for (int py = y0; py < y1; py++) {
        u32 *row = s->pixels + py * s->stride;
        u32 c = 0;
        if (d == 0) {
            c = grad_sample(cf, cv, ct,
                clamp_i(py - y, 0, denom) * 1000 / denom);
            int j = py - y;
            if (r <= 0 || (j >= r && j < h - r)) {
                blend_run_const(row + x0, x1 - x0, c);
                continue;
            }
        }
        for (int px = x0; px < x1; px++) {
            if (d != 0) {
                int i = px - x;
                int pos = i;
                if (d == 2) pos = i + (py - y);
                else if (d == 3) pos = (w - 1 - i) + (py - y);
                c = grad_sample(cf, cv, ct,
                    clamp_i(pos, 0, denom) * 1000 / denom);
            }
            int cov = r <= 0 ? 255
                : zan_round_cov(px - x, py - y, w, h, r, m);
            if (cov <= 0) continue;
            if (cov >= 255) row[px] = blend_over(row[px], c);
            else set_pixel_aa(s, px, py, c, cov);
        }
    }
}

EXPORT void zan_gui_fill_grad_mask(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 dir,
    i32 color_from, i32 color_via, i32 color_to) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_grad)->fill_grad(s, (int)x, (int)y, (int)w, (int)h,
                                     (int)radius, (int)mask, (int)dir,
                                     (u32)color_from, (u32)color_via,
                                     (u32)color_to);
}

/* 抗锯齿圆角矩形掩模采样与混合 */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask) {
    if (cr <= 0) return 255;
    if (cr > rw / 2) cr = rw / 2;
    if (cr > rh / 2) cr = rh / 2;
    if (cr <= 0) return 255;
    if ((i >= cr && i < rw - cr) || (j >= cr && j < rh - cr)) return 255;
    int cx, cy, bit;
    if (i < cr && j < cr)              { cx = cr;      cy = cr;      bit = 1; }
    else if (i >= rw - cr && j < cr)   { cx = rw - cr; cy = cr;      bit = 2; }
    else if (i < cr && j >= rh - cr)   { cx = cr;      cy = rh - cr; bit = 8; }
    else                               { cx = rw - cr; cy = rh - cr; bit = 4; }
    if (!(cmask & bit)) return 255;                 /* square (welded) corner */
    double dx = (double)i + 0.5 - (double)cx;
    double dy = (double)j + 0.5 - (double)cy;
    double dist = sqrt(dx * dx + dy * dy);
    double cov = (double)cr - dist + 0.5;
    if (cov >= 1.0) return 255;
    if (cov <= 0.0) return 0;
    return (int)(cov * 255.0);
}

static void blur_put(zan_surface_t *s, int px, int py, u32 c, int cov) {
    if (cov <= 0 || clipped_out(s, px, py)) return;
    if (cov >= 255) { s->pixels[py * s->stride + px] = c; return; }
    set_pixel_aa(s, px, py, c, cov);
}

static void zan_blur_rect_core(zan_surface_t *s, int x, int y, int w, int h,
                               int radius, int cr, int cmask, u32 *capture) {
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    if (!clip_hits(s, x0, y0, x1, y1)) return;
    int r = radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    ZAN_STAT(g_st_blur, (long long)rw * (long long)rh);
    int ds = 1;
    if (r >= 12) { ds = 4; } else if (r >= 6) { ds = 2; }
    int dw = (rw + ds - 1) / ds;
    int dh = (rh + ds - 1) / ds;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    size_t dn = (size_t)dw * (size_t)dh;
    static u32 *scratch_a = NULL, *scratch_b = NULL;
    static size_t scratch_n = 0;
    if (dn > scratch_n) {
        u32 *na = (u32 *)realloc(scratch_a, dn * sizeof(u32));
        if (!na) return;
        scratch_a = na;
        u32 *nb = (u32 *)realloc(scratch_b, dn * sizeof(u32));
        if (!nb) return;
        scratch_b = nb;
        scratch_n = dn;
    }
    u32 *a = scratch_a;
    u32 *b = scratch_b;

    /* 双线性/块均值下采样缩放 */
    for (int dj = 0; dj < dh; dj++) {
        int sy0 = dj * ds;
        int sy1 = sy0 + ds; if (sy1 > rh) sy1 = rh;
        for (int di = 0; di < dw; di++) {
            int sx0 = di * ds;
            int sx1 = sx0 + ds; if (sx1 > rw) sx1 = rw;
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                u32 *row = s->pixels + (y0 + yy) * s->stride + x0;
                for (int xx = sx0; xx < sx1; xx++) {
                    u32 px = row[xx];
                    sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
                    cnt++;
                }
            }
            if (cnt < 1) cnt = 1;
            a[dj * dw + di] = 0xFF000000u | ((u32)(sr / cnt) << 16)
                            | ((u32)(sg / cnt) << 8) | (u32)(sb / cnt);
        }
    }

    int rr = r / ds; if (rr < 1) rr = 1;
    int win = 2 * rr + 1;
    for (int p = 0; p < 2; p++) {
        /* horizontal: a -> b */
        for (int j = 0; j < dh; j++) {
            u32 *src = a + j * dw;
            u32 *dst = b + j * dw;
            int sr = 0, sg = 0, sb = 0;
            for (int k = -rr; k <= rr; k++) {
                int ii = clamp_i(k, 0, dw - 1);
                u32 px = src[ii];
                sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
            }
            for (int i = 0; i < dw; i++) {
                dst[i] = 0xFF000000u | ((u32)(sr / win) << 16)
                       | ((u32)(sg / win) << 8) | (u32)(sb / win);
                u32 pa = src[clamp_i(i + rr + 1, 0, dw - 1)];
                u32 ps = src[clamp_i(i - rr, 0, dw - 1)];
                sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
                sg += (int)((pa >> 8) & 0xFF)  - (int)((ps >> 8) & 0xFF);
                sb += (int)(pa & 0xFF)         - (int)(ps & 0xFF);
            }
        }
        /* vertical: b -> a */
        for (int i = 0; i < dw; i++) {
            int sr = 0, sg = 0, sb = 0;
            for (int k = -rr; k <= rr; k++) {
                int jj = clamp_i(k, 0, dh - 1);
                u32 px = b[jj * dw + i];
                sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
            }
            for (int j = 0; j < dh; j++) {
                a[j * dw + i] = 0xFF000000u | ((u32)(sr / win) << 16)
                              | ((u32)(sg / win) << 8) | (u32)(sb / win);
                u32 pa = b[clamp_i(j + rr + 1, 0, dh - 1) * dw + i];
                u32 ps = b[clamp_i(j - rr, 0, dh - 1) * dw + i];
                sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
                sg += (int)((pa >> 8) & 0xFF)  - (int)((ps >> 8) & 0xFF);
                sb += (int)(pa & 0xFF)         - (int)(ps & 0xFF);
            }
        }
    }

    /* Vibrancy (luma-preserving ~1 */
    for (int i = 0; i < (int)dn; i++) {
        u32 px = a[i];
        int rc = (int)((px >> 16) & 0xFF);
        int gc = (int)((px >> 8) & 0xFF);
        int bc = (int)(px & 0xFF);
        int luma = (rc * 77 + gc * 150 + bc * 29) >> 8;
        rc = luma + (rc - luma) * 8 / 5;
        gc = luma + (gc - luma) * 8 / 5;
        bc = luma + (bc - luma) * 8 / 5;
        a[i] = 0xFF000000u | ((u32)clamp_i(rc, 0, 255) << 16)
             | ((u32)clamp_i(gc, 0, 255) << 8) | (u32)clamp_i(bc, 0, 255);
    }

    if (ds == 1) {
        for (int j = 0; j < rh; j++) {
            u32 *src = a + j * dw;
            for (int i = 0; i < rw; i++) {
                u32 px = src[i];
                int nz = ((((x0 + i) * 197 + (y0 + j) * 173) >> 3) % 7) - 3;
                int rC = clamp_i((int)((px >> 16) & 0xFF) + nz, 0, 255);
                int gC = clamp_i((int)((px >> 8) & 0xFF) + nz, 0, 255);
                int bC = clamp_i((int)(px & 0xFF) + nz, 0, 255);
                u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
                blur_put(s, x0 + i, y0 + j, out,
                         cr <= 0 ? 255 : zan_round_cov(i, j, rw, rh, cr, cmask));
                if (capture)
                    capture[(size_t)j * rw + i] =
                        s->pixels[(y0 + j) * s->stride + x0 + i];
            }
        }
    } else {
        for (int j = 0; j < rh; j++) {
            int fy = ((j * 2 + 1) * 256) / (ds * 2) - 128;
            if (fy < 0) fy = 0;
            int gy = fy >> 8; int wy = fy & 255;
            if (gy > dh - 1) { gy = dh - 1; wy = 0; }
            int gy1 = gy + 1; if (gy1 > dh - 1) gy1 = dh - 1;
            for (int i = 0; i < rw; i++) {
                int fx = ((i * 2 + 1) * 256) / (ds * 2) - 128;
                if (fx < 0) fx = 0;
                int gx = fx >> 8; int wx = fx & 255;
                if (gx > dw - 1) { gx = dw - 1; wx = 0; }
                int gx1 = gx + 1; if (gx1 > dw - 1) gx1 = dw - 1;
                u32 p00 = a[gy * dw + gx];
                u32 p01 = a[gy * dw + gx1];
                u32 p10 = a[gy1 * dw + gx];
                u32 p11 = a[gy1 * dw + gx1];
                int w00 = (256 - wx) * (256 - wy);
                int w01 = wx * (256 - wy);
                int w10 = (256 - wx) * wy;
                int w11 = wx * wy;
                int rC = ((int)((p00 >> 16) & 0xFF) * w00 + (int)((p01 >> 16) & 0xFF) * w01
                        + (int)((p10 >> 16) & 0xFF) * w10 + (int)((p11 >> 16) & 0xFF) * w11) >> 16;
                int gC = ((int)((p00 >> 8) & 0xFF) * w00 + (int)((p01 >> 8) & 0xFF) * w01
                        + (int)((p10 >> 8) & 0xFF) * w10 + (int)((p11 >> 8) & 0xFF) * w11) >> 16;
                int bC = ((int)(p00 & 0xFF) * w00 + (int)(p01 & 0xFF) * w01
                        + (int)(p10 & 0xFF) * w10 + (int)(p11 & 0xFF) * w11) >> 16;
                int nz = ((((x0 + i) * 197 + (y0 + j) * 173) >> 3) % 7) - 3;
                rC = clamp_i(rC + nz, 0, 255);
                gC = clamp_i(gC + nz, 0, 255);
                bC = clamp_i(bC + nz, 0, 255);
                u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
                blur_put(s, x0 + i, y0 + j, out,
                         cr <= 0 ? 255 : zan_round_cov(i, j, rw, rh, cr, cmask));
                if (capture)
                    capture[(size_t)j * rw + i] =
                        s->pixels[(y0 + j) * s->stride + x0 + i];
            }
        }
    }
}

EXPORT void zan_gui_blur_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            -1, 1, 0, ZAN_CORNERS_ALL);
}

#define ZAN_BLUR_CACHE_SLOTS 256
typedef struct {
    int valid;
    int sid; 
    int x0, y0, rw, rh, r;
    int cr, cmask;
    u32 src_sum;
    u32 *pixels;
    size_t cap;
} zan_blur_cache_t;

static u32 zan_src_sum(zan_surface_t *s, int x0, int y0, int rw, int rh) {
    u32 sum = 2166136261u;
    for (int j = 0; j < rh; j += 8) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        for (int i = 0; i < rw; i += 8) {
            sum = (sum ^ row[i]) * 16777619u;
        }
    }
    return sum ^ (u32)(rw * 31 + rh);
}
static zan_blur_cache_t g_blur_cache[ZAN_BLUR_CACHE_SLOTS];

static long long g_blur_partial_miss;

static void cpu_blur(zan_surface_t *s, int x, int y, int w, int h, int radius,
                     int slot, int dirty, int cr, int cmask) {
    if (slot < 0 || slot >= ZAN_BLUR_CACHE_SLOTS) {
        zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask, NULL);
        return;
    }
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    if (!clip_hits(s, x0, y0, x1, y1)) return;
    int r = radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    zan_blur_cache_t *c = &g_blur_cache[slot];
    size_t n = (size_t)rw * (size_t)rh;

    int geom_ok = c->valid && c->pixels && c->sid == s->id
        && c->x0 == x0 && c->y0 == y0 && c->rw == rw && c->rh == rh
        && c->r == r && c->cr == cr && c->cmask == cmask;
    int covers = clip_covers(s, x0, y0, x1, y1);
    u32 sum = geom_ok ? zan_src_sum(s, x0, y0, rw, rh) : 0;
    if (geom_ok && (!dirty || (covers && sum == c->src_sum))) {
        ZAN_STAT(g_st_blur_hit, (long long)rw * (long long)rh);
        if (covers) {
            for (int j = 0; j < rh; j++)
                memcpy(s->pixels + (size_t)(y0 + j) * s->stride + x0,
                       c->pixels + (size_t)j * rw, (size_t)rw * sizeof(u32));
        } else {
            for (int j = 0; j < rh; j++) {
                u32 *src = c->pixels + (size_t)j * rw;
                for (int i = 0; i < rw; i++)
                    blur_put(s, x0 + i, y0 + j, src[i], 255);
            }
        }
        return;
    }

    u32 fresh = geom_ok ? sum : zan_src_sum(s, x0, y0, rw, rh);
    if (c->cap < n) {
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (np) { c->pixels = np; c->cap = n; }
    }
    zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask,
                       c->cap >= n ? c->pixels : NULL);
    if (!covers) { g_blur_partial_miss++; }
    if (!covers || c->cap < n) { c->valid = 0; return; }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = r;
    c->cr = cr; c->cmask = cmask;
    c->src_sum = fresh;
    c->sid = s->id;
    c->valid = 1;
}

EXPORT i32 zan_gui_blur_partial_miss_take(void) {
    i32 v = (i32)g_blur_partial_miss;
    g_blur_partial_miss = 0;
    return v;
}

EXPORT void zan_gui_blur_rect_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            (int)slot, (int)dirty, 0, ZAN_CORNERS_ALL);
}

EXPORT void zan_gui_blur_round_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty, i32 corner_radius, i32 corner_mask) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            (int)slot, (int)dirty, (int)corner_radius,
                            (int)corner_mask);
}

#define ZAN_SNAP_CACHE_SLOTS 8
#define ZAN_SNAP_CACHE_MAX_SLOTS 512
static zan_blur_cache_t *g_snap_cache;
static int g_snap_cache_count;
#if defined(__ANDROID__)
#define ZAN_SNAP_CACHE_BYTES (16u * 1024u * 1024u)
#else
#define ZAN_SNAP_CACHE_BYTES 0
#endif
static size_t zan_snap_total_bytes(void) {
    size_t total = 0;
    for (int i = 0; i < g_snap_cache_count; i++) {
        if (g_snap_cache[i].pixels)
            total += g_snap_cache[i].cap * sizeof(u32);
    }
    return total;
}
static void zan_snap_ensure(int slot) {
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS) return;
    if (slot < g_snap_cache_count) return;
    int grow = slot + 1;
    if (grow < ZAN_SNAP_CACHE_SLOTS) grow = ZAN_SNAP_CACHE_SLOTS;
    zan_blur_cache_t *np = (zan_blur_cache_t *)realloc(
        g_snap_cache, (size_t)grow * sizeof(zan_blur_cache_t));
    if (!np) return;
    for (int i = g_snap_cache_count; i < grow; i++) {
        np[i].valid = 0; np[i].pixels = NULL; np[i].cap = 0;
    }
    g_snap_cache = np;
    g_snap_cache_count = grow;
}
static zan_blur_cache_t *zan_snap_slot(int slot) {
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS) return NULL;
    zan_snap_ensure(slot);
    if (slot >= g_snap_cache_count) return NULL;
    return &g_snap_cache[slot];
}

static void zan_cache_drop_surface(int sid) {
    for (int i = 0; i < ZAN_BLUR_CACHE_SLOTS; i++) {
        if (g_blur_cache[i].valid && g_blur_cache[i].sid == sid) {
            g_blur_cache[i].valid = 0;
        }
    }
    for (int i = 0; i < g_snap_cache_count; i++) {
        if (g_snap_cache[i].valid && g_snap_cache[i].sid == sid) {
            g_snap_cache[i].valid = 0;
        }
    }
}

static void cpu_snapshot(zan_surface_t *s, int x, int y, int w, int h,
                         int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    ZAN_STAT(g_st_snap, (long long)rw * (long long)rh);
    size_t n = (size_t)rw * (size_t)rh;
    if (c->cap < n) {
#if defined(__ANDROID__)
        size_t keep = c->pixels ? c->cap * sizeof(u32) : 0;
        if (ZAN_SNAP_CACHE_BYTES
            && zan_snap_total_bytes() - keep + n * sizeof(u32)
               > (size_t)ZAN_SNAP_CACHE_BYTES) {
            c->valid = 0;
            return;
        }
#endif
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (!np) { c->valid = 0; return; }
        c->pixels = np;
        c->cap = n;
    }
    for (int j = 0; j < rh; j++) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        u32 *dst = c->pixels + (size_t)j * rw;
        for (int i = 0; i < rw; i++) dst[i] = row[i];
    }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = 0;
    c->sid = s->id;
    c->valid = 1;
}

EXPORT void zan_gui_snapshot_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, snapshot)->snapshot(s, (int)x, (int)y, (int)w, (int)h,
                                    (int)slot);
}

EXPORT void zan_gui_snapshot_patch_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                        i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, snapshot_patch)->snapshot_patch(s, (int)x, (int)y, (int)w,
                                                (int)h, (int)slot);
}

static void cpu_snapshot_patch(zan_surface_t *s, int x, int y, int w, int h,
                               int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    if (!c->valid || !c->pixels || c->sid != s->id) return;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    if (x1 <= x0 || y1 <= y0) return;
    if (x0 < c->x0 || y0 < c->y0
        || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
        return;
    }
    for (int j = y0; j < y1; j++) {
        u32 *row = s->pixels + j * s->stride + x0;
        u32 *dst = c->pixels + (size_t)(j - c->y0) * c->rw + (x0 - c->x0);
        for (int i = 0; i < x1 - x0; i++) dst[i] = row[i];
    }
}

static int cpu_restore_rect(zan_surface_t *s, int x, int y, int w, int h,
                            int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return 0;
    if (!c->valid || !c->pixels || c->sid != s->id
        || c->x0 != x0 || c->y0 != y0 || c->rw != rw || c->rh != rh) {
        return 0;
    }
    int cx0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
    int cy0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
    int cx1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
    int cy1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
    if (cx1 <= cx0 || cy1 <= cy0) return 1;
    ZAN_STAT(g_st_restore, (long long)(cx1 - cx0) * (long long)(cy1 - cy0));
    for (int j = cy0; j < cy1; j++) {
        u32 *row = s->pixels + j * s->stride + cx0;
        u32 *src = c->pixels + (size_t)(j - y0) * rw + (cx0 - x0);
        for (int i = 0; i < cx1 - cx0; i++) row[i] = src[i];
    }
    return 1;
}

/* 局部视口快照区域还原恢复 */
static int cpu_restore_sub_rect(zan_surface_t *s, int x, int y, int w, int h,
                                int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    if (!c->valid || !c->pixels || c->sid != s->id) return 0;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x1 <= x0 || y1 <= y0) return 1;
    if (x0 < c->x0 || y0 < c->y0
        || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
        return 0;
    }
    if (x0 < s->clip_x0) x0 = s->clip_x0;
    if (y0 < s->clip_y0) y0 = s->clip_y0;
    if (x1 > s->clip_x1) x1 = s->clip_x1;
    if (y1 > s->clip_y1) y1 = s->clip_y1;
    x0 = clamp_i(x0, 0, s->width);
    y0 = clamp_i(y0, 0, s->height);
    x1 = clamp_i(x1, 0, s->width);
    y1 = clamp_i(y1, 0, s->height);
    if (x1 <= x0 || y1 <= y0) return 1;
    ZAN_STAT(g_st_restore, (long long)(x1 - x0) * (long long)(y1 - y0));
    for (int j = y0; j < y1; j++) {
        u32 *row = s->pixels + j * s->stride + x0;
        u32 *src = c->pixels + (size_t)(j - c->y0) * c->rw + (x0 - c->x0);
        for (int i = 0; i < x1 - x0; i++) row[i] = src[i];
    }
    return 1;
}

static int cpu_restore(zan_surface_t *s, int x, int y, int w, int h, int slot,
                      int sub_rect) {
    return sub_rect ? cpu_restore_sub_rect(s, x, y, w, h, slot)
                    : cpu_restore_rect(s, x, y, w, h, slot);
}

EXPORT i32 zan_gui_restore_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    return ZAN_IMPL(s, restore)->restore(s, (int)x, (int)y, (int)w, (int)h,
                                         (int)slot, 0);
}

EXPORT i32 zan_gui_restore_sub_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    return ZAN_IMPL(s, restore)->restore(s, (int)x, (int)y, (int)w, (int)h,
                                         (int)slot, 1);
}

EXPORT i32 zan_gui_surface_dump(i32 surface_id, const char *path) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels || !path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int w = s->width, h = s->height;
    int stride_out = (w * 3 + 3) & ~3;
    u32 data_size = (u32)stride_out * (u32)h;
    u32 file_size = 54 + data_size;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &file_size, 4);
    u32 off = 54; memcpy(hdr + 10, &off, 4);
    u32 ihs = 40; memcpy(hdr + 14, &ihs, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    unsigned short planes = 1; memcpy(hdr + 26, &planes, 2);
    unsigned short bpp = 24; memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &data_size, 4);
    fwrite(hdr, 1, 54, f);
    unsigned char *row = (unsigned char *)malloc((size_t)stride_out);
    if (!row) { fclose(f); return 0; }
    memset(row, 0, (size_t)stride_out);
    for (int j = h - 1; j >= 0; j--) {
        u32 *src = s->pixels + (size_t)j * (size_t)s->stride;
        for (int i = 0; i < w; i++) {
            u32 p = src[i];
            row[i * 3 + 0] = (unsigned char)(p & 0xFF);
            row[i * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
            row[i * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
        }
        fwrite(row, 1, (size_t)stride_out, f);
    }
    free(row);
    fclose(f);
    return 1;
}

EXPORT void zan_gui_surface_release(i32 surface_id, i32 slot) {
    (void)surface_id;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    free(c->pixels);
    c->pixels = NULL;
    c->cap = 0;
    c->valid = 0;
}
