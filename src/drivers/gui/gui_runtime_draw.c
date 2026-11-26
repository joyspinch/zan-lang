static void cpu_draw_square_rect(zan_surface_t *s, int x, int y, int w, int h,
                                 u32 c, int t) {
  /* top */ cpu_fill_rect(s, x, y, w, t, c);
  /* bottom */ cpu_fill_rect(s, x, y + h - t, w, t, c);
  /* left */ cpu_fill_rect(s, x, y + t, t, h - 2 * t, c);
  /* right */ cpu_fill_rect(s, x + w - t, y + t, t, h - 2 * t, c);
}

EXPORT void zan_gui_draw_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                              i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, draw_round)
      ->draw_round(s, (int)x, (int)y, (int)w, (int)h, 0, ZAN_CORNERS_ALL,
                   (u32)color, (int)thickness);
}

#define ZAN_CORNER_TL 1
#define ZAN_CORNER_TR 2
#define ZAN_CORNER_BR 4
#define ZAN_CORNER_BL 8

#include "gui_runtime_initial_fill.c"

static void zan_shape_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                                 int radius, int mask, u32 c, int initial) {
  int r = radius;
  int ix = x, iy = y, iw = w, ih = h;
  int m = mask;
  region_write(s, x, y, (int64_t)x + w, (int64_t)y + h);
  if (r <= 0 || (m & 15) == 0) {
    zan_shape_fill_rect(s, x, y, w, h, c, initial);
    return;
  }
  if (r > iw / 2)
    r = iw / 2;
  if (r > ih / 2)
    r = ih / 2;
  ZAN_STAT(g_st_round, (long long)iw * (long long)ih);

  int xl = ix + r, xr = ix + iw - r;
  int yt = iy + r, yb = iy + ih - r;
  zan_shape_fill_rect(s, xl, iy, iw - 2 * r, r, c, initial);
  zan_shape_fill_rect(s, ix, yt, iw, ih - 2 * r, c, initial); /* middle */
  zan_shape_fill_rect(s, xl, yb, iw - 2 * r, r, c, initial);

  double cc[4][2] = {
      {xl, yt}, /* TL, square px<xl, py<yt */
      {xr, yt}, /* TR, square px>=xr, py<yt */
      {xl, yb}, /* BL, square px<xl, py>=yb */
      {xr, yb}  /* BR, square px>=xr, py>=yb */
  };
  int zx[4] = {ix, xr, ix, xr};
  int zy[4] = {iy, iy, yb, yb};
  int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
  double rout = (double)r + 0.5;
  double rout2 = rout * rout;
  double rin = (double)r - 0.5;
  double rin2 = rin > 0.0 ? rin * rin : -1.0;
  u32 sa = (c >> 24) & 0xFFu;
  u32 rgb = c & 0x00FFFFFFu;
  for (int ci = 0; ci < 4; ci++) {
    if (!(m & bit[ci])) {
      zan_shape_fill_rect(s, zx[ci], zy[ci], r, r, c, initial);
      continue;
    }
    int cx0 = clamp_i(zx[ci], s->clip_x0, s->clip_x1);
    int cy0 = clamp_i(zy[ci], s->clip_y0, s->clip_y1);
    int cx1 = clamp_i(zx[ci] + r, s->clip_x0, s->clip_x1);
    int cy1 = clamp_i(zy[ci] + r, s->clip_y0, s->clip_y1);
    if (cx1 <= cx0 || cy1 <= cy0)
      continue;
    double ccx = cc[ci][0], ccy = cc[ci][1];
    for (int py = cy0; py < cy1; py++) {
      double ddy = (double)py + 0.5 - ccy;
      double ddy2 = ddy * ddy;
      if (ddy2 >= rout2)
        continue;
      u32 *row = s->pixels + py * s->stride;
      for (int px = cx0; px < cx1; px++) {
        double ddx = (double)px + 0.5 - ccx;
        double d2 = ddx * ddx + ddy2;
        if (d2 >= rout2)
          continue;
        if (d2 <= rin2) {
          if (initial)
            zan_initial_fill_pixel(s, px, py, c, 255);
          else
            row[px] = blend_over(row[px], c);
          continue;
        }
        double cov = (double)r - sqrt(d2) + 0.5;
        if (cov >= 1.0) {
          if (initial)
            zan_initial_fill_pixel(s, px, py, c, 255);
          else
            row[px] = blend_over(row[px], c);
        } else if (cov > 0.0) {
          int cov_i = (int)(cov * 255.0);
          if (cov_i > 0) {
            if (initial) {
              zan_initial_fill_pixel(s, px, py, c, cov_i);
            } else {
              u32 prod = sa * (u32)cov_i;
              u32 a = (prod + 1u + (prod >> 8)) >> 8;
              if (a)
                row[px] = blend_over(row[px], (a << 24) | rgb);
            }
          }
        }
      }
    }
  }
}

static void cpu_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int mask, u32 c) {
  zan_shape_fill_round(s, x, y, w, h, radius, mask, c, 0);
}

/* Internal shape background entry; other backends retain their old painter. */
EXPORT void zan_gui_fill_initial_shape(i32 surface_id, i32 x, i32 y, i32 w,
                                       i32 h, i32 radius, i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count || w <= 0 || h <= 0)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  if (s->be && s->be != &zan_cpu_backend) {
    ZAN_IMPL(s, fill_round)->fill_round(s, x, y, w, h, radius, 15, (u32)color);
    return;
  }
  zan_shape_fill_round(s, x, y, w, h, radius, 15, (u32)color, 1);
}

EXPORT void zan_gui_fill_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w,
                                           i32 h, i32 radius, i32 mask,
                                           i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_round)
      ->fill_round(s, (int)x, (int)y, (int)w, (int)h, (int)radius, (int)mask,
                   (u32)color);
}

EXPORT void zan_gui_fill_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                      i32 h, i32 radius, i32 color) {
  zan_gui_fill_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color);
}

static u32 surface_round_color(u32 fill, u32 border, int outer, int inner) {
  uint64_t af = fill >> 24, ab = border >> 24;
  uint64_t bw = ab * (uint64_t)(outer - inner);
  uint64_t fw = af * ((uint64_t)outer * 255u - bw);
  bw *= 255u;
  uint64_t weight = fw + bw;
  if (!weight)
    return 0;
  u32 alpha = (u32)(weight / (255u * 255u));
  u32 rgb = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    uint64_t c =
        ((fill >> shift) & 255u) * fw + ((border >> shift) & 255u) * bw;
    rgb |= (u32)(c / weight) << shift;
  }
  return (alpha << 24) | rgb;
}

static void cpu_blend_clamped_rect(zan_surface_t *s, int rx0, int ry0,
                                   int rx1, int ry1, u32 col) {
  if ((col >> 24) == 0)
    return;
  if (rx0 < s->clip_x0)
    rx0 = s->clip_x0;
  if (ry0 < s->clip_y0)
    ry0 = s->clip_y0;
  if (rx1 > s->clip_x1)
    rx1 = s->clip_x1;
  if (ry1 > s->clip_y1)
    ry1 = s->clip_y1;
  int rw = rx1 - rx0;
  if (rw <= 0 || ry1 <= ry0)
    return;
  for (int py = ry0; py < ry1; py++)
    blend_run_const(s->pixels + py * s->stride + rx0, rw, col);
}

static void cpu_surface_round_box(zan_surface_t *s, int x, int y, int w, int h,
                                  int r, int t, int iw, int ih, int ir,
                                  int mask, u32 fill, u32 border, int bx0,
                                  int by0, int bx1, int by1) {
  if (bx1 <= bx0 || by1 <= by0)
    return;
  for (int py = by0; py < by1; py++) {
    u32 *row = s->pixels + py * s->stride;
    int j = py - y;
    for (int px = bx0; px < bx1; px++) {
      int i = px - x;
      int outer = zan_round_cov(i, j, w, h, r, mask);
      if (!outer)
        continue;
      int inner = 0;
      if (iw > 0 && ih > 0 && i >= t && j >= t && i - t < iw && j - t < ih)
        inner = zan_round_cov(i - t, j - t, iw, ih, ir, mask);
      if (inner > outer)
        inner = outer;
      u32 c =
          inner == outer
              ? ((fill & 0xFFFFFFu) | ((u32)((fill >> 24) * outer / 255) << 24))
              : surface_round_color(fill, border, outer, inner);
      row[px] = blend_over(row[px], c);
    }
  }
}

static void cpu_surface_round(zan_surface_t *s, int x, int y, int w, int h,
                              int radius, int mask, u32 fill, u32 border,
                              int thickness) {
  if (w <= 0 || h <= 0)
    return;
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
  if (x1 <= x0 || y1 <= y0)
    return;
  region_write(s, x0, y0, x1, y1);
  ZAN_STAT(g_st_round, (long long)(x1 - x0) * (y1 - y0));
  int m = r > t ? r : t;
  if (iw > 0 && ih > 0 && 2 * m < w && 2 * m < h) {
    u32 edge_col = surface_round_color(fill, border, 255, 0);
    /* 4 straight border edges (outer=255, inner=0) */
    cpu_blend_clamped_rect(s, x + m, y, x + w - m, y + t, edge_col);
    cpu_blend_clamped_rect(s, x + m, y + h - t, x + w - m, y + h, edge_col);
    cpu_blend_clamped_rect(s, x, y + m, x + t, y + h - m, edge_col);
    cpu_blend_clamped_rect(s, x + w - t, y + m, x + w, y + h - m, edge_col);
    /* 3 pure interior fill strips (outer=255, inner=255) */
    if (m > t) {
      cpu_blend_clamped_rect(s, x + m, y + t, x + w - m, y + m, fill);
      cpu_blend_clamped_rect(s, x + m, y + h - m, x + w - m, y + h - t, fill);
    }
    cpu_blend_clamped_rect(s, x + t, y + m, x + w - t, y + h - m, fill);
    /* 4 corner squares (m x m) run exact AA loop */
    cpu_surface_round_box(s, x, y, w, h, r, t, iw, ih, ir, mask, fill, border,
                          x0, y0, x + m < x1 ? x + m : x1,
                          y + m < y1 ? y + m : y1);
    cpu_surface_round_box(s, x, y, w, h, r, t, iw, ih, ir, mask, fill, border,
                          x + w - m > x0 ? x + w - m : x0, y0, x1,
                          y + m < y1 ? y + m : y1);
    cpu_surface_round_box(s, x, y, w, h, r, t, iw, ih, ir, mask, fill, border,
                          x0, y + h - m > y0 ? y + h - m : y0,
                          x + m < x1 ? x + m : x1, y1);
    cpu_surface_round_box(s, x, y, w, h, r, t, iw, ih, ir, mask, fill, border,
                          x + w - m > x0 ? x + w - m : x0,
                          y + h - m > y0 ? y + h - m : y0, x1, y1);
    return;
  }
  cpu_surface_round_box(s, x, y, w, h, r, t, iw, ih, ir, mask, fill, border,
                        x0, y0, x1, y1);
}

EXPORT void zan_gui_surface_rounded_rect_mask(i32 surface_id, i32 x, i32 y,
                                              i32 w, i32 h, i32 radius,
                                              i32 mask, i32 fill, i32 border,
                                              i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || w <= 0 || h <= 0)
    return;
  ZAN_IMPL(s, surface_round)
      ->surface_round(s, x, y, w, h, radius, mask, (u32)fill, (u32)border,
                      thickness);
}

EXPORT void zan_gui_surface_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                         i32 h, i32 radius, i32 fill,
                                         i32 border, i32 thickness) {
  zan_gui_surface_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, fill,
                                    border, thickness);
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
  region_write(s, x, y, (int64_t)x + w, (int64_t)y + h);
  if (th < 1)
    th = 1;
  if (r > iw / 2)
    r = iw / 2;
  if (r > ih / 2)
    r = ih / 2;
  if (th > r)
    th = r;

  int xl = ix + r, xr = ix + iw - r;
  int yt = iy + r, yb = iy + ih - r;
  cpu_fill_rect(s, xl, iy, iw - 2 * r, th, c);           /* top */
  cpu_fill_rect(s, xl, iy + ih - th, iw - 2 * r, th, c); /* bottom */
  cpu_fill_rect(s, ix, yt, th, ih - 2 * r, c);           /* left */
  cpu_fill_rect(s, ix + iw - th, yt, th, ih - 2 * r, c); /* right */

  double cc[4][2] = {{xl, yt}, {xr, yt}, {xl, yb}, {xr, yb}};
  int zx[4] = {ix, xr, ix, xr};
  int zy[4] = {iy, iy, yb, yb};
  int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
  int hx[4] = {ix, xr, ix, xr};
  double rin = (double)r - (double)th;
  double rout = (double)r + 0.5;
  double rout2 = rout * rout;
  double rin_lo = rin - 0.5;
  double rin_lo2 = rin_lo > 0.0 ? rin_lo * rin_lo : -1.0;
  double solid_hi = (double)r - 0.5;
  double solid_lo = rin + 0.5;
  double solid_hi2 = solid_hi > solid_lo ? solid_hi * solid_hi : -1.0;
  double solid_lo2 = solid_hi > solid_lo ? solid_lo * solid_lo : 1e30;
  u32 sa = (c >> 24) & 0xFFu;
  u32 rgb = c & 0x00FFFFFFu;
  for (int ci = 0; ci < 4; ci++) {
    if (!(m & bit[ci])) {
      int cy = (ci < 2) ? zy[ci] : zy[ci] + r - th;
      int cx = (ci % 2 == 0) ? hx[ci] : hx[ci] + r - th;
      cpu_fill_rect(s, zx[ci], cy, r, th, c);
      cpu_fill_rect(s, cx, zy[ci], th, r, c);
      continue;
    }
    int cx0 = clamp_i(zx[ci], s->clip_x0, s->clip_x1);
    int cy0 = clamp_i(zy[ci], s->clip_y0, s->clip_y1);
    int cx1 = clamp_i(zx[ci] + r, s->clip_x0, s->clip_x1);
    int cy1 = clamp_i(zy[ci] + r, s->clip_y0, s->clip_y1);
    if (cx1 <= cx0 || cy1 <= cy0)
      continue;
    double ccx = cc[ci][0], ccy = cc[ci][1];
    for (int py = cy0; py < cy1; py++) {
      double ddy = (double)py + 0.5 - ccy;
      double ddy2 = ddy * ddy;
      if (ddy2 >= rout2)
        continue;
      u32 *row = s->pixels + py * s->stride;
      for (int px = cx0; px < cx1; px++) {
        double ddx = (double)px + 0.5 - ccx;
        double d2 = ddx * ddx + ddy2;
        if (d2 >= rout2 || d2 <= rin_lo2)
          continue;
        if (d2 >= solid_lo2 && d2 <= solid_hi2) {
          row[px] = blend_over(row[px], c);
          continue;
        }
        double dist = sqrt(d2);
        double outerCov = rout - dist;
        double innerCov = dist - rin_lo;
        double cov = outerCov < innerCov ? outerCov : innerCov;
        if (cov <= 0.0)
          continue;
        if (cov >= 1.0) {
          row[px] = blend_over(row[px], c);
        } else {
          int cov_i = (int)(cov * 255.0);
          if (cov_i > 0) {
            u32 a = (sa * (u32)cov_i) / 255u;
            if (a)
              row[px] = blend_over(row[px], (a << 24) | rgb);
          }
        }
      }
    }
  }
}

EXPORT void zan_gui_draw_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w,
                                           i32 h, i32 radius, i32 mask,
                                           i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, draw_round)
      ->draw_round(s, (int)x, (int)y, (int)w, (int)h, (int)radius, (int)mask,
                   (u32)color, (int)thickness);
}

EXPORT void zan_gui_draw_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                      i32 h, i32 radius, i32 color,
                                      i32 thickness) {
  zan_gui_draw_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color,
                                 thickness);
}

static inline u32 shadow_eval_color(double d, double half, double inv,
                                    u32 c, u32 sa, u32 rgb, int *is_solid) {
  double t = (d + half) * inv;
  if (t <= 0.0) {
    *is_solid = 1;
    return c;
  }
  *is_solid = 0;
  if (t >= 1.0)
    return 0;
  double cov = 1.0 - t * t * (3.0 - 2.0 * t);
  int cov_i = (int)(cov * 255.0);
  if (cov_i <= 0)
    return 0;
  if (cov_i >= 255) {
    *is_solid = 1;
    return c;
  }
  u32 a = (sa * (u32)cov_i) / 255u;
  if (a == 0)
    return 0;
  return (a << 24) | rgb;
}

static inline void shadow_fill_span(u32 *dst, int n, u32 sc) {
  if (n <= 0 || (sc >> 24) == 0)
    return;
  if ((sc >> 24) == 255u)
    fill_run_const(dst, n, sc);
  else
    blend_run_const(dst, n, sc);
}

static void shadow_fill_lr_band(zan_surface_t *s, int bx0, int bx1,
                                int by0, int by1, double cx, double ex,
                                double r, double half, double inv,
                                u32 c, u32 sa, u32 rgb) {
  int bw = bx1 - bx0;
  if (bw <= 0 || by1 <= by0)
    return;
  u32 ramp_buf[256];
  for (int off = 0; off < bw; off += 256) {
    int n = bw - off;
    if (n > 256)
      n = 256;
    int any = 0;
    for (int k = 0; k < n; k++) {
      double qx = fabs((double)(bx0 + off + k) + 0.5 - cx) - ex;
      int solid = 0;
      u32 sc = shadow_eval_color(qx - r, half, inv, c, sa, rgb, &solid);
      ramp_buf[k] = sc;
      if (sc)
        any = 1;
    }
    if (!any)
      continue;
    for (int py = by0; py < by1; py++) {
      u32 *row = s->pixels + py * s->stride + bx0 + off;
      for (int k = 0; k < n; k++) {
        u32 sc = ramp_buf[k];
        if (sc)
          row[k] = blend_over(row[k], sc);
      }
    }
  }
}

static void shadow_fill_corner(zan_surface_t *s, int bx0, int bx1,
                               int by0, int by1, double cx, double cy,
                               double ex, double ey, double r,
                               double half, double inv,
                               u32 c, u32 sa, u32 rgb) {
  if (bx1 <= bx0 || by1 <= by0)
    return;
  double max_d = r + half;
  double max_d2 = max_d * max_d;
  double min_d = r - half;
  double min_d2 = min_d > 0.0 ? min_d * min_d : -1.0;
  for (int py = by0; py < by1; py++) {
    double qy = fabs((double)py + 0.5 - cy) - ey;
    double qy2 = qy * qy;
    if (qy2 >= max_d2)
      continue;
    u32 *row = s->pixels + py * s->stride;
    for (int px = bx0; px < bx1; px++) {
      double qx = fabs((double)px + 0.5 - cx) - ex;
      double d2 = qx * qx + qy2;
      if (d2 >= max_d2)
        continue;
      if (d2 <= min_d2) {
        row[px] = blend_over(row[px], c);
        continue;
      }
      int solid = 0;
      u32 sc = shadow_eval_color(sqrt(d2) - r, half, inv, c, sa, rgb, &solid);
      if (sc)
        row[px] = blend_over(row[px], sc);
    }
  }
}

static void cpu_shadow_round(zan_surface_t *s, int x, int y, int w, int h,
                             int radius, int blur, u32 c) {
  int iw = w, ih = h;
  if (iw <= 0 || ih <= 0)
    return;
  int bl = blur;
  if (bl <= 0) {
    cpu_fill_round(s, x, y, w, h, radius, ZAN_CORNERS_ALL, c);
    return;
  }
  u32 sa = (c >> 24) & 0xFFu;
  if (sa == 0)
    return;
  u32 rgb = c & 0x00FFFFFFu;

  int r = radius;
  if (r > iw / 2)
    r = iw / 2;
  if (r > ih / 2)
    r = ih / 2;
  if (r < 0)
    r = 0;

  double cx = (double)x + (double)iw / 2.0;
  double cy = (double)y + (double)ih / 2.0;
  double ex = (double)iw / 2.0 - (double)r;
  double ey = (double)ih / 2.0 - (double)r;

  int x0 = x - bl, x1 = x + iw + bl;
  int y0 = y - bl, y1 = y + ih + bl;
  region_write(s, x0, y0, x1, y1);
  if (x0 < s->clip_x0)
    x0 = s->clip_x0;
  if (y0 < s->clip_y0)
    y0 = s->clip_y0;
  if (x1 > s->clip_x1)
    x1 = s->clip_x1;
  if (y1 > s->clip_y1)
    y1 = s->clip_y1;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > s->width)
    x1 = s->width;
  if (y1 > s->height)
    y1 = s->height;
  if (x0 >= x1 || y0 >= y1)
    return;

  double half = (double)bl / 2.0;
  double inv = 1.0 / (double)bl;
  double rd = (double)r;

  /* 9-region partition along the rounded-rect inner box [x+r, x+iw-r) x [y+r, y+ih-r). */
  int lx0 = x0, lx1 = clamp_i(x + r, x0, x1);
  int mx0 = lx1, mx1 = clamp_i(x + iw - r, lx1, x1);
  int rx0 = mx1, rx1 = x1;

  int ty0 = y0, ty1 = clamp_i(y + r, y0, y1);
  int my0 = ty1, my1 = clamp_i(y + ih - r, ty1, y1);
  int by0 = my1, by1 = y1;

  /* 1 & 2. Top-center and bottom-center straight bands: row-constant SIMD spans, 0 sqrt. */
  if (mx1 > mx0) {
    int mw = mx1 - mx0;
    for (int py = ty0; py < ty1; py++) {
      double qy = fabs((double)py + 0.5 - cy) - ey;
      int solid = 0;
      u32 sc = shadow_eval_color(qy - rd, half, inv, c, sa, rgb, &solid);
      if (sc)
        shadow_fill_span(s->pixels + py * s->stride + mx0, mw, sc);
    }
    for (int py = by0; py < by1; py++) {
      double qy = fabs((double)py + 0.5 - cy) - ey;
      int solid = 0;
      u32 sc = shadow_eval_color(qy - rd, half, inv, c, sa, rgb, &solid);
      if (sc)
        shadow_fill_span(s->pixels + py * s->stride + mx0, mw, sc);
    }
  }

  /* 3 & 4. Left-middle and right-middle straight bands: 1D X ramp, 0 sqrt. */
  if (my1 > my0) {
    shadow_fill_lr_band(s, lx0, lx1, my0, my1, cx, ex, rd, half, inv, c, sa, rgb);
    shadow_fill_lr_band(s, rx0, rx1, my0, my1, cx, ex, rd, half, inv, c, sa, rgb);

    /* 5. Center-middle region: qx <= 0 and qy <= 0, so d = max(qx, qy) - r (0 sqrt). */
    if (mx1 > mx0) {
      int half_i = (bl + 1) / 2;
      int solid_x0 = clamp_i(x + half_i, mx0, mx1);
      int solid_x1 = clamp_i(x + iw - half_i, solid_x0, mx1);
      int solid_y0 = clamp_i(y + half_i, my0, my1);
      int solid_y1 = clamp_i(y + ih - half_i, solid_y0, my1);
      if (solid_x1 > solid_x0 && solid_y1 > solid_y0) {
        int sw = solid_x1 - solid_x0;
        for (int py = solid_y0; py < solid_y1; py++)
          shadow_fill_span(s->pixels + py * s->stride + solid_x0, sw, c);
      } else {
        solid_x0 = solid_x1 = solid_y0 = solid_y1 = 0;
      }
      if (solid_x0 != mx0 || solid_x1 != mx1 ||
          solid_y0 != my0 || solid_y1 != my1) {
        for (int py = my0; py < my1; py++) {
          double qy = fabs((double)py + 0.5 - cy) - ey;
          int in_sy = (py >= solid_y0 && py < solid_y1);
          u32 *row = s->pixels + py * s->stride;
          for (int px = mx0; px < mx1; px++) {
            if (in_sy && px >= solid_x0 && px < solid_x1) {
              px = solid_x1 - 1;
              continue;
            }
            double qx = fabs((double)px + 0.5 - cx) - ex;
            double d = (qx > qy ? qx : qy) - rd;
            int solid = 0;
            u32 sc = shadow_eval_color(d, half, inv, c, sa, rgb, &solid);
            if (sc)
              row[px] = blend_over(row[px], sc);
          }
        }
      }
    }
  }

  /* 6..9. Four corner squares: tight clipped direct pointer loop + early radial reject. */
  shadow_fill_corner(s, lx0, lx1, ty0, ty1, cx, cy, ex, ey, rd, half, inv, c, sa, rgb);
  shadow_fill_corner(s, rx0, rx1, ty0, ty1, cx, cy, ex, ey, rd, half, inv, c, sa, rgb);
  shadow_fill_corner(s, lx0, lx1, by0, by1, cx, cy, ex, ey, rd, half, inv, c, sa, rgb);
  shadow_fill_corner(s, rx0, rx1, by0, by1, cx, cy, ex, ey, rd, half, inv, c, sa, rgb);
}

EXPORT void zan_gui_shadow_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                        i32 h, i32 radius, i32 blur,
                                        i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, shadow_round)
      ->shadow_round(s, (int)x, (int)y, (int)w, (int)h, (int)radius, (int)blur,
                     (u32)color);
}

/* 抗锯齿填充圆光栅化（亚像素采样） */
static void cpu_fill_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c) {
  region_write_clip(s);
  int r = radius;
  double rr = (double)r;
  double rin = rr - 0.5, rout = rr + 0.5;
  double rin2 = rin * rin, rout2 = rout * rout;

  ZAN_STAT(g_st_round, (long long)(2 * r + 3) * (long long)(2 * r + 3));
  u32 sa = (c >> 24) & 0xFFu;
  if (sa == 0)
    return;
  u32 rgb = c & 0x00FFFFFFu;
  int ext = r + 2;
  int ylo = cy - ext, yhi = cy + ext;
  if (ylo < s->clip_y0)
    ylo = s->clip_y0;
  if (yhi >= s->clip_y1)
    yhi = s->clip_y1 - 1;
  for (int py = ylo; py <= yhi; py++) {
    double wy = (double)py + 0.5 - (double)cy;
    double wy2 = wy * wy;
    if (wy2 > rout2)
      continue;
    u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
    double out_hs = sqrt(rout2 - wy2);
    int xlo = cx + (int)floor(-out_hs - 0.5);
    int xhi = cx + (int)ceil(out_hs - 0.5);
    if (xlo < cx - ext)
      xlo = cx - ext;
    if (xhi > cx + ext)
      xhi = cx + ext;
    if (xlo < s->clip_x0)
      xlo = s->clip_x0;
    if (xhi >= s->clip_x1)
      xhi = s->clip_x1 - 1;
    if (xlo > xhi)
      continue;

    if (rin2 > wy2) {
      double hs = sqrt(rin2 - wy2);
      int L = cx + (int)ceil(-hs - 0.5);
      int Rr = cx + (int)floor(hs - 0.5);
      int sL = L > xlo ? L : xlo;
      int sR = Rr < xhi ? Rr : xhi;
      if (sL <= sR) {
        for (int px = xlo; px < sL; px++) {
          double wx = (double)px + 0.5 - (double)cx;
          double cov = rout - sqrt(wx * wx + wy2);
          if (cov > 0.0) {
            int cov_i = (int)(cov * 255.0);
            if (cov_i > 0) {
              u32 prod = sa * (u32)cov_i;
              u32 a = (prod + 1u + (prod >> 8)) >> 8;
              if (a)
                row[px] = blend_over(row[px], (a << 24) | rgb);
            }
          }
        }
        shadow_fill_span(row + sL, sR - sL + 1, c);
        for (int px = sR + 1; px <= xhi; px++) {
          double wx = (double)px + 0.5 - (double)cx;
          double cov = rout - sqrt(wx * wx + wy2);
          if (cov > 0.0) {
            int cov_i = (int)(cov * 255.0);
            if (cov_i > 0) {
              u32 prod = sa * (u32)cov_i;
              u32 a = (prod + 1u + (prod >> 8)) >> 8;
              if (a)
                row[px] = blend_over(row[px], (a << 24) | rgb);
            }
          }
        }
        continue;
      }
    }
    for (int px = xlo; px <= xhi; px++) {
      double wx = (double)px + 0.5 - (double)cx;
      double cov = rout - sqrt(wx * wx + wy2);
      if (cov <= 0.0)
        continue;
      if (cov >= 1.0) {
        row[px] = blend_over(row[px], c);
      } else {
        int cov_i = (int)(cov * 255.0);
        if (cov_i > 0) {
          u32 a = (sa * (u32)cov_i) / 255u;
          if (a)
            row[px] = blend_over(row[px], (a << 24) | rgb);
        }
      }
    }
  }
}

EXPORT void zan_gui_fill_circle(i32 surface_id, i32 cx, i32 cy, i32 radius,
                                i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_circle)
      ->fill_circle(s, (int)cx, (int)cy, (int)radius, (u32)color);
}

static void cpu_draw_circle(zan_surface_t *s, int cx, int cy, int radius, u32 c,
                            int thickness) {
  region_write_clip(s);
  int r = radius;
  u32 sa = (c >> 24) & 0xFFu;
  if (r <= 0 || sa == 0)
    return;
  u32 rgb = c & 0x00FFFFFFu;
  double rr = (double)r;
  double half = (double)(thickness > 0 ? thickness : 1) * 0.5;
  double rout = rr + half + 0.5;
  double rin_out = rr - half - 0.5;
  double rout2 = rout * rout;
  double rin_out2 = rin_out > 0.0 ? rin_out * rin_out : -1.0;
  double solid_hi = rr + half - 0.5;
  double solid_lo = rr - half + 0.5;
  double solid_hi2 = solid_hi > 0.0 ? solid_hi * solid_hi : -1.0;
  double solid_lo2 = solid_lo > 0.0 ? solid_lo * solid_lo : -1.0;
  int has_solid = (half >= 0.5);
  int ext = r + (int)half + 2;

  int ylo = cy - ext, yhi = cy + ext;
  if (ylo < s->clip_y0)
    ylo = s->clip_y0;
  if (yhi >= s->clip_y1)
    yhi = s->clip_y1 - 1;
  if (ylo > yhi)
    return;

  for (int py = ylo; py <= yhi; py++) {
    double wy = (double)py + 0.5 - (double)cy;
    double wy2 = wy * wy;
    if (wy2 > rout2)
      continue;
    double out_hs = sqrt(rout2 - wy2);
    int xlo = cx + (int)floor(-out_hs - 0.5);
    int xhi = cx + (int)ceil(out_hs - 0.5);
    if (xlo < cx - ext)
      xlo = cx - ext;
    if (xhi > cx + ext)
      xhi = cx + ext;
    if (xlo < s->clip_x0)
      xlo = s->clip_x0;
    if (xhi >= s->clip_x1)
      xhi = s->clip_x1 - 1;
    if (xlo > xhi)
      continue;

    int spans[2][2];
    int nspan = 0;
    if (rin_out > 0.0 && wy2 < rin_out2) {
      double in_hs = sqrt(rin_out2 - wy2);
      int hx0 = cx + (int)floor(-in_hs - 0.5);
      int hx1 = cx + (int)ceil(in_hs - 0.5);
      if (xlo <= hx0) {
        spans[nspan][0] = xlo;
        spans[nspan][1] = hx0 < xhi ? hx0 : xhi;
        nspan++;
      }
      if (hx1 <= xhi) {
        spans[nspan][0] = hx1 > xlo ? hx1 : xlo;
        spans[nspan][1] = xhi;
        nspan++;
      }
    } else {
      spans[0][0] = xlo;
      spans[0][1] = xhi;
      nspan = 1;
    }

    u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
    for (int si = 0; si < nspan; si++) {
      int sx0 = spans[si][0], sx1 = spans[si][1];
      for (int px = sx0; px <= sx1; px++) {
        double wx = (double)px + 0.5 - (double)cx;
        double d2 = wx * wx + wy2;
        if (d2 > rout2 || (rin_out > 0.0 && d2 < rin_out2))
          continue;
        if (has_solid && d2 <= solid_hi2 &&
            (solid_lo <= 0.0 || d2 >= solid_lo2)) {
          row[px] = (sa == 255u) ? c : blend_over(row[px], c);
          continue;
        }
        double d = fabs(sqrt(d2) - rr);
        if (d <= half - 0.5) {
          row[px] = (sa == 255u) ? c : blend_over(row[px], c);
        } else if (d <= half + 0.5) {
          int cov = (int)((half + 0.5 - d) * 255.0);
          if (cov > 0) {
            if (cov > 255)
              cov = 255;
            u32 a = (sa * (u32)cov) / 255u;
            if (a)
              row[px] = blend_over(row[px], (a << 24) | rgb);
          }
        }
      }
    }
  }
}

EXPORT void zan_gui_draw_circle(i32 surface_id, i32 cx, i32 cy, i32 radius,
                                i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, draw_circle)
      ->draw_circle(s, (int)cx, (int)cy, (int)radius, (u32)color,
                    (int)thickness);
}

static inline u32 zan_blend_over_opaque_fast(u32 dst, u32 sr, u32 sg, u32 sb,
                                             u32 a) {
  u32 inv = 255u - a;
  u32 dr = (dst >> 16) & 0xFFu;
  u32 dg = (dst >> 8) & 0xFFu;
  u32 db = dst & 0xFFu;
  u32 xr = sr * a + dr * inv;
  u32 xg = sg * a + dg * inv;
  u32 xb = sb * a + db * inv;
  u32 or_ = (xr + 1u + (xr >> 8)) >> 8;
  u32 og = (xg + 1u + (xg >> 8)) >> 8;
  u32 ob = (xb + 1u + (xb >> 8)) >> 8;
  return 0xFF000000u | (or_ << 16) | (og << 8) | ob;
}

static void cpu_fill_radial(zan_surface_t *s, int cx, int cy, int radius,
                            u32 color, int inner_a) {
  region_write_clip(s);
  int r = radius;
  if (r <= 0)
    return;
  int icx = cx, icy = cy;
  u32 rgb = color & 0x00FFFFFFu;
  u32 sr = (rgb >> 16) & 0xFFu, sg = (rgb >> 8) & 0xFFu, sb = rgb & 0xFFu;
  int peak = inner_a;
  if (peak > 255)
    peak = 255;
  if (peak <= 0)
    return;
  int r2 = r * r;
  /* 48-bit reciprocal is 0-LSB exact vs ((r2 - d2) << 8) / r2 for all r <= 1024 */
  int use_recip = (r <= 1024);
  uint64_t inv_r2 = use_recip
                        ? (((1ULL << 48) + (uint64_t)r2 - 1ULL) / (uint64_t)r2)
                        : 0ULL;
  ZAN_STAT(g_st_radial, (long long)(2 * r + 1) * (long long)(2 * r + 1));
  int y_lo = icy - r, y_hi = icy + r;
  if (y_lo < s->clip_y0)
    y_lo = s->clip_y0;
  if (y_hi > s->clip_y1 - 1)
    y_hi = s->clip_y1 - 1;
#if ZAN_HAS_SSE2
  const __m128i v_zero = _mm_setzero_si128();
  const __m128i v_255 = _mm_set1_epi16(255);
  const __m128i v_one = _mm_set1_epi16(1);
  const __m128i v_src16 = _mm_setr_epi16((short)sb, (short)sg, (short)sr, 255,
                                         (short)sb, (short)sg, (short)sr, 255);
  const __m128i v_amask = _mm_set1_epi32((int)0xFF000000u);
#endif
  for (int py = y_lo; py <= y_hi; py++) {
    int dy = py - icy;
    int dy2 = dy * dy;
    int span = r2 - dy2;
    if (span <= 0)
      continue;
    int dxMax = (int)sqrt((double)span);
    while (dxMax > 0 && dxMax * dxMax >= span)
      dxMax--;
    while ((dxMax + 1) * (dxMax + 1) < span)
      dxMax++;
    int x_lo = icx - dxMax, x_hi = icx + dxMax;
    if (x_lo < s->clip_x0)
      x_lo = s->clip_x0;
    if (x_hi > s->clip_x1 - 1)
      x_hi = s->clip_x1 - 1;
    u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
    if (use_recip) {
      int px = x_lo;
#if ZAN_HAS_SSE2
      /* SSE2 4 像素批处理：仅在 4 像素目标全不透明时走精确 0-LSB 向量混色 */
      for (; px + 3 <= x_hi; px += 4) {
        __m128i dst4 = _mm_loadu_si128((const __m128i *)(row + px));
        __m128i a_eq = _mm_cmpeq_epi32(_mm_and_si128(dst4, v_amask), v_amask);
        if (_mm_movemask_epi8(a_eq) != 0xFFFF) {
          for (int k = 0; k < 4; k++) {
            int dx = (px + k) - icx;
            int rem = span - dx * dx;
            int t = (int)(((uint64_t)rem * inv_r2) >> 40);
            int a = (peak * t * t) >> 16;
            if (a > 0)
              row[px + k] = blend_over(row[px + k], ((u32)a << 24) | rgb);
          }
          continue;
        }
        int dx0 = px - icx;
        int rem0 = span - dx0 * dx0;
        int rem1 = span - (dx0 + 1) * (dx0 + 1);
        int rem2 = span - (dx0 + 2) * (dx0 + 2);
        int rem3 = span - (dx0 + 3) * (dx0 + 3);
        int t0 = (int)(((uint64_t)rem0 * inv_r2) >> 40);
        int t1 = (int)(((uint64_t)rem1 * inv_r2) >> 40);
        int t2 = (int)(((uint64_t)rem2 * inv_r2) >> 40);
        int t3 = (int)(((uint64_t)rem3 * inv_r2) >> 40);
        int a0 = (peak * t0 * t0) >> 16;
        int a1 = (peak * t1 * t1) >> 16;
        int a2 = (peak * t2 * t2) >> 16;
        int a3 = (peak * t3 * t3) >> 16;
        if ((a0 | a1 | a2 | a3) == 0)
          continue;
        __m128i va_lo = _mm_setr_epi16((short)a0, (short)a0, (short)a0, 0,
                                       (short)a1, (short)a1, (short)a1, 0);
        __m128i va_hi = _mm_setr_epi16((short)a2, (short)a2, (short)a2, 0,
                                       (short)a3, (short)a3, (short)a3, 0);
        __m128i vi_lo = _mm_sub_epi16(v_255, va_lo);
        __m128i vi_hi = _mm_sub_epi16(v_255, va_hi);
        __m128i d_lo = _mm_unpacklo_epi8(dst4, v_zero);
        __m128i d_hi = _mm_unpackhi_epi8(dst4, v_zero);
        __m128i s_lo = _mm_add_epi16(_mm_mullo_epi16(v_src16, va_lo),
                                     _mm_mullo_epi16(d_lo, vi_lo));
        __m128i s_hi = _mm_add_epi16(_mm_mullo_epi16(v_src16, va_hi),
                                     _mm_mullo_epi16(d_hi, vi_hi));
        s_lo = _mm_srli_epi16(
            _mm_add_epi16(_mm_add_epi16(s_lo, v_one), _mm_srli_epi16(s_lo, 8)), 8);
        s_hi = _mm_srli_epi16(
            _mm_add_epi16(_mm_add_epi16(s_hi, v_one), _mm_srli_epi16(s_hi, 8)), 8);
        __m128i out = _mm_or_si128(_mm_packus_epi16(s_lo, s_hi), v_amask);
        _mm_storeu_si128((__m128i *)(row + px), out);
      }
#endif
      for (; px <= x_hi; px++) {
        int dx = px - icx;
        int rem = span - dx * dx;
        int t = (int)(((uint64_t)rem * inv_r2) >> 40);
        int a = (peak * t * t) >> 16;
        if (a <= 0)
          continue;
        u32 d = row[px];
        row[px] = ((d >> 24) == 255u)
                      ? zan_blend_over_opaque_fast(d, sr, sg, sb, (u32)a)
                      : blend_over(d, ((u32)a << 24) | rgb);
      }
    } else {
      for (int px = x_lo; px <= x_hi; px++) {
        int dx = px - icx;
        int d2 = dx * dx + dy2;
        int t = ((r2 - d2) << 8) / r2;
        int a = (peak * t * t) >> 16;
        if (a <= 0)
          continue;
        u32 d = row[px];
        row[px] = ((d >> 24) == 255u)
                      ? zan_blend_over_opaque_fast(d, sr, sg, sb, (u32)a)
                      : blend_over(d, ((u32)a << 24) | rgb);
      }
    }
  }
}

EXPORT void zan_gui_fill_radial(i32 surface_id, i32 cx, i32 cy, i32 radius,
                                i32 color, i32 inner_a) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_radial)
      ->fill_radial(s, (int)cx, (int)cy, (int)radius, (u32)color, (int)inner_a);
}

EXPORT void zan_gui_fill_rects(i32 surface_id, const i32 *data, i32 count) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !data || count < 1)
    return;
  for (i32 i = 0; i < count; i++) {
    const i32 *d = data + i * 5;
    ZAN_IMPL(s, fill_rect)
        ->fill_rect(s, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (u32)d[4]);
  }
}

EXPORT void zan_gui_fill_circles(i32 surface_id, const i32 *data, i32 count) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !data || count < 1)
    return;
  for (i32 i = 0; i < count; i++) {
    const i32 *d = data + i * 4;
    ZAN_IMPL(s, fill_circle)
        ->fill_circle(s, (int)d[0], (int)d[1], (int)d[2], (u32)d[3]);
  }
}

EXPORT void zan_gui_fill_radials(i32 surface_id, const i32 *data, i32 count) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !data || count < 1)
    return;
  for (i32 i = 0; i < count; i++) {
    const i32 *d = data + i * 5;
    ZAN_IMPL(s, fill_radial)
        ->fill_radial(s, (int)d[0], (int)d[1], (int)d[2], (u32)d[3], (int)d[4]);
  }
}

EXPORT void zan_gui_blit_surface(i32 dst_id, i32 src_id, i32 dst_x, i32 dst_y,
                                 i32 src_x, i32 src_y, i32 w, i32 h) {
  if (dst_id < 0 || dst_id >= g_surface_count)
    return;
  if (src_id < 0 || src_id >= g_surface_count)
    return;
  zan_surface_t *d = g_surfaces[dst_id];
  zan_surface_t *s = g_surfaces[src_id];
  if (!d || !s || !d->pixels || !s->pixels)
    return;
  if (w <= 0 || h <= 0)
    return;
  if (s->be && s->be->sync_to_cpu)
    s->be->sync_to_cpu(s);
  if (d->be && d->be->sync_to_cpu)
    d->be->sync_to_cpu(d);
  int sx = (int)src_x, sy = (int)src_y, dx = (int)dst_x, dy = (int)dst_y;
  int cw = (int)w, ch = (int)h;
  if (sx < 0) {
    dx -= sx;
    cw += sx;
    sx = 0;
  }
  if (sy < 0) {
    dy -= sy;
    ch += sy;
    sy = 0;
  }
  if (sx + cw > s->width)
    cw = s->width - sx;
  if (sy + ch > s->height)
    ch = s->height - sy;
  if (dx < d->clip_x0) {
    int cut = d->clip_x0 - dx;
    dx += cut;
    sx += cut;
    cw -= cut;
  }
  if (dy < d->clip_y0) {
    int cut = d->clip_y0 - dy;
    dy += cut;
    sy += cut;
    ch -= cut;
  }
  if (dx + cw > d->clip_x1)
    cw = d->clip_x1 - dx;
  if (dy + ch > d->clip_y1)
    ch = d->clip_y1 - dy;
  if (cw <= 0 || ch <= 0)
    return;
  region_read(s, sx, sy, sx + cw, sy + ch);
  region_write(d, dx, dy, dx + cw, dy + ch);
  for (int row = 0; row < ch; row++) {
    u32 *from = s->pixels + (size_t)(sy + row) * (size_t)s->stride + (size_t)sx;
    u32 *to = d->pixels + (size_t)(dy + row) * (size_t)d->stride + (size_t)dx;
    memcpy(to, from, (size_t)cw * sizeof(u32));
  }
  if (d->be && d->be->sync_from_cpu)
    d->be->sync_from_cpu(d);
}

static void cpu_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                            int r_outer, int a0_deg, int a1_deg, u32 c) {
  region_write_clip(s);
  if (r_outer <= 0)
    return;
  double a0 = (double)a0_deg, a1 = (double)a1_deg;
  if (a1 < a0) {
    double tmp = a0;
    a0 = a1;
    a1 = tmp;
  }
  double sweep = a1 - a0;
  if (sweep <= 0.0)
    return;
  if (sweep > 360.0)
    sweep = 360.0;
  a0 = a0 - floor(a0 / 360.0) * 360.0;
  a1 = a0 + sweep;

  const double PI = 3.14159265358979323846;
  double ri = (double)r_inner;
  if (ri < 0.0)
    ri = 0.0;
  double ro = (double)r_outer;
  int hasHole = ri > 0.0;
  double roOut = ro + 0.5, roIn = ro - 0.5;
  double riOut = ri - 0.5, riIn = ri + 0.5;
  if (riOut < 0.0)
    riOut = 0.0;
  double roOut2 = roOut * roOut, roIn2 = roIn * roIn;
  double riOut2 = riOut * riOut, riIn2 = riIn * riIn;
  if (roIn2 < 0.0)
    roIn2 = 0.0;
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
    cand[n][0] = e0x * ro;
    cand[n][1] = e0y * ro;
    n++;
    cand[n][0] = e1x * ri;
    cand[n][1] = e1y * ri;
    n++;
    cand[n][0] = e1x * ro;
    cand[n][1] = e1y * ro;
    n++;
    if (!hasHole) {
      cand[n][0] = 0.0;
      cand[n][1] = 0.0;
      n++;
    }
    for (int k = 0; k <= 8; k++) {
      double ca = (double)(k * 90);
      if (ca < a0 || ca > a1)
        continue;
      cand[n][0] = sin(ca * PI / 180.0) * ro;
      cand[n][1] = -cos(ca * PI / 180.0) * ro;
      n++;
    }
    for (int i = 0; i < n; i++) {
      if (cand[i][0] < xmn)
        xmn = cand[i][0];
      if (cand[i][0] > xmx)
        xmx = cand[i][0];
      if (cand[i][1] < ymn)
        ymn = cand[i][1];
      if (cand[i][1] > ymx)
        ymx = cand[i][1];
    }
    int nx0 = cx + (int)floor(xmn) - 1, nx1 = cx + (int)ceil(xmx) + 1;
    int ny0 = cy + (int)floor(ymn) - 1, ny1 = cy + (int)ceil(ymx) + 1;
    if (nx0 > bx0)
      bx0 = nx0;
    if (nx1 < bx1)
      bx1 = nx1;
    if (ny0 > by0)
      by0 = ny0;
    if (ny1 < by1)
      by1 = ny1;
  }
  if (bx0 < s->clip_x0)
    bx0 = s->clip_x0;
  if (by0 < s->clip_y0)
    by0 = s->clip_y0;
  if (bx1 > s->clip_x1 - 1)
    bx1 = s->clip_x1 - 1;
  if (by1 > s->clip_y1 - 1)
    by1 = s->clip_y1 - 1;

  for (int py = by0; py <= by1; py++) {
    double wy = (double)py + 0.5 - (double)cy;
    double wy2 = wy * wy;
    if (wy2 > roOut2)
      continue;
    double half = sqrt(roOut2 - wy2);
    int rx0 = (int)ceil((double)cx - half - 0.5);
    int rx1 = (int)floor((double)cx + half - 0.5);
    if (rx0 < bx0)
      rx0 = bx0;
    if (rx1 > bx1)
      rx1 = bx1;
    int spans[2][2];
    int nspan = 0;
    if (hasHole && wy2 < riOut2) {
      double hi = sqrt(riOut2 - wy2);
      int hx0 = (int)floor((double)cx - hi - 0.5);
      int hx1 = (int)ceil((double)cx + hi - 0.5);
      spans[nspan][0] = rx0;
      spans[nspan][1] = hx0 < rx1 ? hx0 : rx1;
      nspan++;
      spans[nspan][0] = hx1 > rx0 ? hx1 : rx0;
      spans[nspan][1] = rx1;
      nspan++;
    } else {
      spans[nspan][0] = rx0;
      spans[nspan][1] = rx1;
      nspan++;
    }
    for (int sp = 0; sp < nspan; sp++) {
      int x0 = spans[sp][0], x1 = spans[sp][1];
      if (x0 < bx0)
        x0 = bx0;
      if (x1 > bx1)
        x1 = bx1;
      for (int px = x0; px <= x1; px++) {
        double wx = (double)px + 0.5 - (double)cx;
        double d2 = wx * wx + wy2;
        if (d2 > roOut2)
          continue;
        if (hasHole && d2 < riOut2)
          continue;
        double radCov = 1.0;
        if (d2 > roIn2)
          radCov = roOut - sqrt(d2);
        else if (hasHole && d2 < riIn2)
          radCov = sqrt(d2) - riOut;
        if (radCov <= 0.0)
          continue;
        if (radCov > 1.0)
          radCov = 1.0;
        double angCov = 1.0;
        if (!full) {
          if (wide) {
            double gLo = e1x * wy - e1y * wx;
            double gHi = wx * e0y - wy * e0x;
            double covLo = gLo + 0.5, covHi = gHi + 0.5;
            if (covLo > 1.0)
              covLo = 1.0;
            if (covLo < 0.0)
              covLo = 0.0;
            if (covHi > 1.0)
              covHi = 1.0;
            if (covHi < 0.0)
              covHi = 0.0;
            angCov = 1.0 - covLo * covHi;
          } else {
            double covLo = (e0x * wy - e0y * wx) + 0.5;
            double covHi = (wx * e1y - wy * e1x) + 0.5;
            if (covLo <= 0.0 || covHi <= 0.0)
              continue;
            if (covLo > 1.0)
              covLo = 1.0;
            if (covHi > 1.0)
              covHi = 1.0;
            angCov = covLo * covHi;
          }
          if (angCov <= 0.0)
            continue;
        }
        int cov = (int)(radCov * angCov * 255.0);
        if (cov >= 255)
          set_pixel(s, px, py, c);
        else
          set_pixel_aa(s, px, py, c, cov);
      }
    }
  }
}

EXPORT void zan_gui_fill_sector(i32 surface_id, i32 cx, i32 cy, i32 r_inner,
                                i32 r_outer, i32 a0_deg, i32 a1_deg,
                                i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_sector)
      ->fill_sector(s, (int)cx, (int)cy, (int)r_inner, (int)r_outer,
                    (int)a0_deg, (int)a1_deg, (u32)color);
}

/* 吴小林抗锯齿直线光栅化算法 */
static void cpu_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                          u32 c, int thickness) {
  int t = thickness;
  u32 sa = (c >> 24) & 0xFFu;
  if (sa == 0)
    return;

  if (s->region.state >= REGION_WATCHING) {
    int64_t pad = (int64_t)(t > 1 ? t : 1) + 2;
    int64_t left = (x0 < x1 ? x0 : x1) - pad, top = (y0 < y1 ? y0 : y1) - pad;
    region_write(s, left < 0 ? 0 : (int)left, top < 0 ? 0 : (int)top,
                 (x0 > x1 ? x0 : x1) + pad, (y0 > y1 ? y0 : y1) + pad);
  }
  if (t <= 1) {
    /* Fast axis-aligned 1px paths (SIMD span for horizontal, direct stride for vertical) */
    if (y0 == y1) {
      if (y0 < s->clip_y0 || y0 >= s->clip_y1)
        return;
      int lx = x0 < x1 ? x0 : x1;
      int rx = x0 > x1 ? x0 : x1;
      if (lx < s->clip_x0)
        lx = s->clip_x0;
      if (rx >= s->clip_x1)
        rx = s->clip_x1 - 1;
      if (lx <= rx) {
        u32 *dst = s->pixels + (size_t)y0 * (size_t)s->stride + lx;
        int n = rx - lx + 1;
        if (sa == 255u)
          fill_run_const(dst, n, c);
        else
          blend_run_const(dst, n, c);
      }
      return;
    }
    if (x0 == x1) {
      if (x0 < s->clip_x0 || x0 >= s->clip_x1)
        return;
      int ty = y0 < y1 ? y0 : y1;
      int by = y0 > y1 ? y0 : y1;
      if (ty < s->clip_y0)
        ty = s->clip_y0;
      if (by >= s->clip_y1)
        by = s->clip_y1 - 1;
      for (int py = ty; py <= by; py++) {
        u32 *dst = s->pixels + (size_t)py * (size_t)s->stride + x0;
        *dst = (sa == 255u) ? c : blend_over(*dst, c);
      }
      return;
    }
    int steep = abs(y1 - y0) > abs(x1 - x0);
    double fx0 = (double)x0, fy0 = (double)y0, fx1 = (double)x1,
           fy1 = (double)y1;
    if (steep) {
      double tmp;
      tmp = fx0;
      fx0 = fy0;
      fy0 = tmp;
      tmp = fx1;
      fx1 = fy1;
      fy1 = tmp;
    }
    if (fx0 > fx1) {
      double tmp;
      tmp = fx0;
      fx0 = fx1;
      fx1 = tmp;
      tmp = fy0;
      fy0 = fy1;
      fy1 = tmp;
    }
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
    u32 rgb = c & 0x00FFFFFFu;
    double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
    double len2 = dx * dx + dy * dy;
    double inv_len2 = len2 > 0.0001 ? 1.0 / len2 : 0.0;
    double half = t * 0.5;
    int lox = (x0 < x1 ? x0 : x1);
    int hix = (x0 > x1 ? x0 : x1);
    int loy = (y0 < y1 ? y0 : y1);
    int hiy = (y0 > y1 ? y0 : y1);
    int minx = lox - t - 1, maxx = hix + t + 1;
    int miny = loy - t - 1, maxy = hiy + t + 1;
    double ux = 0.0, uy = 0.0, seglen = sqrt(len2);
    if (seglen > 0.0001) {
      ux = dx / seglen;
      uy = dy / seglen;
    }
    double rr = half + 0.5;
    double rr2 = rr * rr;
    double in_r = half - 0.5;
    double in_r2 = in_r > 0.0 ? in_r * in_r : -1.0;
    if (miny < s->clip_y0)
      miny = s->clip_y0;
    if (maxy > s->clip_y1 - 1)
      maxy = s->clip_y1 - 1;
    if (minx < s->clip_x0)
      minx = s->clip_x0;
    if (maxx > s->clip_x1 - 1)
      maxx = s->clip_x1 - 1;
    for (int py = miny; py <= maxy; py++) {
      double yc = (double)py + 0.5;
      double lo = 1e30, hi = -1e30;
      for (int e = 0; e < 2; e++) {
        double ecx = e ? (double)x1 : (double)x0;
        double ecy = e ? (double)y1 : (double)y0;
        double dyc = yc - ecy;
        double dyc2 = dyc * dyc;
        if (dyc2 > rr2)
          continue;
        double half_w = sqrt(rr2 - dyc2);
        if (ecx - half_w < lo)
          lo = ecx - half_w;
        if (ecx + half_w > hi)
          hi = ecx + half_w;
      }
      if (seglen > 0.0001) {
        double nx = -uy * rr, ny = ux * rr;
        for (int e = 0; e < 2; e++) {
          double sgn = e ? -1.0 : 1.0;
          double ax = (double)x0 + sgn * nx, ay = (double)y0 + sgn * ny;
          double bx = (double)x1 + sgn * nx, by = (double)y1 + sgn * ny;
          double span = by - ay;
          if (span > -0.0001 && span < 0.0001)
            continue;
          double tt = (yc - ay) / span;
          if (tt < 0.0 || tt > 1.0)
            continue;
          double xs = ax + (bx - ax) * tt;
          if (xs < lo)
            lo = xs;
          if (xs > hi)
            hi = xs;
        }
      }
      if (lo > hi)
        continue;
      int rowLo = (int)floor(lo) - 1, rowHi = (int)ceil(hi) + 1;
      if (rowLo < minx)
        rowLo = minx;
      if (rowHi > maxx)
        rowHi = maxx;
      u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
      double py_dy = (yc - (double)y0) * dy;
      for (int px = rowLo; px <= rowHi; px++) {
        double pxc = (double)px + 0.5;
        double proj = 0.0;
        if (inv_len2 > 0.0) {
          proj = ((pxc - (double)x0) * dx + py_dy) * inv_len2;
          if (proj < 0.0)
            proj = 0.0;
          else if (proj > 1.0)
            proj = 1.0;
        }
        double ddx = pxc - ((double)x0 + proj * dx);
        double ddy = yc - ((double)y0 + proj * dy);
        double d2 = ddx * ddx + ddy * ddy;
        if (d2 >= rr2)
          continue;
        if (d2 <= in_r2) {
          row[px] = (sa == 255u) ? c : blend_over(row[px], c);
          continue;
        }
        double cov = rr - sqrt(d2);
        if (cov <= 0.0)
          continue;
        int icov = aa_coverage(cov);
        if (icov >= 255) {
          row[px] = (sa == 255u) ? c : blend_over(row[px], c);
        } else if (icov > 0) {
          u32 a = (sa * (u32)icov) / 255u;
          if (a)
            row[px] = blend_over(row[px], (a << 24) | rgb);
        }
      }
    }
  }
}

EXPORT void zan_gui_draw_line(i32 surface_id, i32 x0, i32 y0, i32 x1, i32 y1,
                              i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, draw_line)
      ->draw_line(s, (int)x0, (int)y0, (int)x1, (int)y1, (u32)color,
                  (int)thickness);
}

static u8 *g_poly_cov = NULL;
static i32 g_poly_cov_w = 0;
static i32 g_poly_cov_h = 0;
static i32 *g_poly_dirty = NULL;
static i32 g_poly_dirty_n = 0;
static i32 g_poly_dirty_cap = 0;

/* 连续折线批处理光栅化（逐行精确水平跨度 + 裁剪交集 + 平方距离早退） */
static void zan_polyline_core(zan_surface_t *s, const i32 *pts, i32 n, u32 c,
                              int t, int shift) {
  int W = s->width, H = s->height;
  u32 sa = (c >> 24) & 0xFFu;
  if (W <= 0 || H <= 0 || sa == 0)
    return;
  int cx0 = s->clip_x0 > 0 ? s->clip_x0 : 0;
  int cy0 = s->clip_y0 > 0 ? s->clip_y0 : 0;
  int cx1 = s->clip_x1 < W ? s->clip_x1 - 1 : W - 1;
  int cy1 = s->clip_y1 < H ? s->clip_y1 - 1 : H - 1;
  if (cx0 > cx1 || cy0 > cy1)
    return;
  u32 rgb = c & 0x00FFFFFFu;
  double scale = shift == 0 ? 1.0 : (1.0 / 256.0);

  if (g_poly_cov_w < W || g_poly_cov_h < H) {
    free(g_poly_cov);
    g_poly_cov = (u8 *)malloc((size_t)W * (size_t)H);
    g_poly_cov_w = W;
    g_poly_cov_h = H;
    if (g_poly_cov)
      memset(g_poly_cov, 0, (size_t)W * (size_t)H);
  }
  if (!g_poly_cov) {
    g_poly_cov_w = 0;
    g_poly_cov_h = 0;
    return;
  }
  g_poly_dirty_n = 0;

  double half = t * 0.5;
  double rr = half + 0.5;
  double rr2 = rr * rr;
  double in_r = half - 0.5;
  double in_r2 = in_r > 0.0 ? in_r * in_r : -1.0;

  for (i32 i = 0; i + 1 < n; i++) {
    double x0 = pts[i * 2] * scale, y0 = pts[i * 2 + 1] * scale;
    double x1 = pts[i * 2 + 2] * scale, y1 = pts[i * 2 + 3] * scale;
    if (x0 == x1 && y0 == y1)
      continue;
    double dx = x1 - x0, dy = y1 - y0;
    double len2 = dx * dx + dy * dy;
    if (len2 < 0.0001)
      continue;
    double inv_len2 = 1.0 / len2;
    double seglen = sqrt(len2);
    double ux = dx / seglen, uy = dy / seglen;
    double nx = -uy * rr, ny = ux * rr;
    int minx = (int)floor(x0 < x1 ? x0 : x1) - t - 2;
    int maxx = (int)ceil(x0 > x1 ? x0 : x1) + t + 2;
    int miny = (int)floor(y0 < y1 ? y0 : y1) - t - 2;
    int maxy = (int)ceil(y0 > y1 ? y0 : y1) + t + 2;
    if (minx < cx0)
      minx = cx0;
    if (miny < cy0)
      miny = cy0;
    if (maxx > cx1)
      maxx = cx1;
    if (maxy > cy1)
      maxy = cy1;
    if (minx > maxx || miny > maxy)
      continue;

    for (int py = miny; py <= maxy; py++) {
      double pyc = (double)py + 0.5;
      double lo = 1e30, hi = -1e30;
      for (int e = 0; e < 2; e++) {
        double ecx = e ? x1 : x0;
        double ecy = e ? y1 : y0;
        double dyc = pyc - ecy;
        double dyc2 = dyc * dyc;
        if (dyc2 > rr2)
          continue;
        double hw = sqrt(rr2 - dyc2);
        if (ecx - hw < lo)
          lo = ecx - hw;
        if (ecx + hw > hi)
          hi = ecx + hw;
      }
      for (int e = 0; e < 2; e++) {
        double sgn = e ? -1.0 : 1.0;
        double ax = x0 + sgn * nx, ay = y0 + sgn * ny;
        double bx = x1 + sgn * nx, by = y1 + sgn * ny;
        double span = by - ay;
        if (span > -0.0001 && span < 0.0001)
          continue;
        double tt = (pyc - ay) / span;
        if (tt < 0.0 || tt > 1.0)
          continue;
        double xs = ax + (bx - ax) * tt;
        if (xs < lo)
          lo = xs;
        if (xs > hi)
          hi = xs;
      }
      if (lo > hi)
        continue;
      int rowLo = (int)floor(lo) - 1, rowHi = (int)ceil(hi) + 1;
      if (rowLo < minx)
        rowLo = minx;
      if (rowHi > maxx)
        rowHi = maxx;
      double py_dy = (pyc - y0) * dy;
      int row_off = py * W;
      for (int px = rowLo; px <= rowHi; px++) {
        double pxc = (double)px + 0.5;
        double proj = ((pxc - x0) * dx + py_dy) * inv_len2;
        if (proj < 0.0)
          proj = 0.0;
        else if (proj > 1.0)
          proj = 1.0;
        double ddx = pxc - (x0 + proj * dx);
        double ddy = pyc - (y0 + proj * dy);
        double d2 = ddx * ddx + ddy * ddy;
        if (d2 >= rr2)
          continue;
        int icov;
        if (d2 <= in_r2) {
          icov = 255;
        } else {
          double cov = rr - sqrt(d2);
          if (cov <= 0.0)
            continue;
          icov = aa_coverage(cov);
          if (icov <= 0)
            continue;
        }
        int idx = row_off + px;
        u8 prev = g_poly_cov[idx];
        if (prev == 0) {
          if (g_poly_dirty_n >= g_poly_dirty_cap) {
            i32 cap2 = g_poly_dirty_cap ? g_poly_dirty_cap * 2 : 1024;
            i32 *nd = (i32 *)realloc(g_poly_dirty, (size_t)cap2 * sizeof(i32));
            if (!nd) {
              memset(g_poly_cov, 0, (size_t)W * (size_t)H);
              g_poly_dirty_n = 0;
              return;
            }
            g_poly_dirty = nd;
            g_poly_dirty_cap = cap2;
          }
          g_poly_dirty[g_poly_dirty_n++] = (py << 16) | (px & 0xFFFF);
          g_poly_cov[idx] = (u8)icov;
        } else if (icov > prev) {
          g_poly_cov[idx] = (u8)icov;
        }
      }
    }
  }

  for (i32 d = 0; d < g_poly_dirty_n; d++) {
    int px = g_poly_dirty[d] & 0xFFFF;
    int py = (g_poly_dirty[d] >> 16) & 0xFFFF;
    int idx = py * W + px;
    int cov = g_poly_cov[idx];
    g_poly_cov[idx] = 0;
    u32 *dst = s->pixels + (size_t)py * (size_t)s->stride + px;
    if (cov >= 255) {
      *dst = (sa == 255u) ? c : blend_over(*dst, c);
    } else {
      u32 a = (sa * (u32)cov) / 255u;
      if (a)
        *dst = blend_over(*dst, (a << 24) | rgb);
    }
  }
}

EXPORT void zan_gui_draw_polyline(i32 surface_id, const i32 *pts, i32 n,
                                  i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !pts || n < 2)
    return;
  int t = (int)thickness;
  if (t < 1)
    t = 1;
  region_write_clip(s);
  ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 0);
}

EXPORT void zan_gui_draw_polyline_fx(i32 surface_id, const i32 *pts, i32 n,
                                     i32 color, i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !pts || n < 2)
    return;
  int t = (int)thickness;
  if (t < 1)
    t = 1;
  region_write_clip(s);
  ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 8);
}

static void cpu_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                         int t, int shift) {
  zan_polyline_core(s, pts, (i32)n, c, t, shift);
}

EXPORT void zan_gui_draw_polybatch(i32 surface_id, const i32 *pts,
                                   const i32 *counts, i32 n_paths, i32 color,
                                   i32 thickness) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !pts || !counts || n_paths < 1)
    return;
  int t = (int)thickness;
  if (t < 1)
    t = 1;
  region_write_clip(s);
  if (s->be && s->be->polybatch) {
    s->be->polybatch(s, pts, counts, (int)n_paths, (u32)color, t, 0);
    return;
  }
  i32 off = 0;
  if (s->be && s->be->polyline) {
    for (i32 p = 0; p < n_paths; p++) {
      i32 n = counts[p];
      if (n >= 2)
        s->be->polyline(s, pts + off, (int)n, (u32)color, t, 0);
      off += n * 2;
    }
    return;
  }
  for (i32 p = 0; p < n_paths; p++) {
    i32 n = counts[p];
    if (n >= 2)
      cpu_polyline(s, pts + off, (int)n, (u32)color, t, 0);
    off += n * 2;
  }
}

static void cpu_composite_surface(zan_surface_t *d, zan_surface_t *s,
                                  int x, int y, int w, int h,
                                  int scale_pm, int deg_tenths,
                                  int opacity_pm) {
  if (!d || !s || !d->pixels || !s->pixels || d == s)
    return;
  if (w > s->width)
    w = s->width;
  if (h > s->height)
    h = s->height;
  if (opacity_pm > 1000)
    opacity_pm = 1000;
  if (w <= 0 || h <= 0 || scale_pm <= 0 || opacity_pm <= 0)
    return;

  int deg = deg_tenths % 3600;
  if (deg < 0)
    deg += 3600;

  if (deg == 0 && scale_pm == 1000) {
    int sx0 = 0, sy0 = 0, dx0 = x, dy0 = y, cw = w, ch = h;
    if (dx0 < d->clip_x0) {
      int cut = d->clip_x0 - dx0;
      dx0 += cut;
      sx0 += cut;
      cw -= cut;
    }
    if (dy0 < d->clip_y0) {
      int cut = d->clip_y0 - dy0;
      dy0 += cut;
      sy0 += cut;
      ch -= cut;
    }
    if (dx0 + cw > d->clip_x1)
      cw = d->clip_x1 - dx0;
    if (dy0 + ch > d->clip_y1)
      ch = d->clip_y1 - dy0;
    if (cw <= 0 || ch <= 0)
      return;
    region_read(s, sx0, sy0, sx0 + cw, sy0 + ch);
    region_write(d, dx0, dy0, dx0 + cw, dy0 + ch);
    for (int row = 0; row < ch; row++) {
      const u32 *src_row =
          s->pixels + (size_t)(sy0 + row) * (size_t)s->stride + (size_t)sx0;
      u32 *dst_row =
          d->pixels + (size_t)(dy0 + row) * (size_t)d->stride + (size_t)dx0;
      for (int col = 0; col < cw; col++) {
        u32 sp = src_row[col];
        u32 sa = sp >> 24;
        if (sa == 0)
          continue;
        if (opacity_pm < 1000)
          sa = (sa * (u32)opacity_pm + 500u) / 1000u;
        if (sa == 0)
          continue;
        if (sa == 255u) {
          dst_row[col] = 0xFF000000u | (sp & 0x00FFFFFFu);
        } else {
          dst_row[col] = blend_over(dst_row[col], (sa << 24) | (sp & 0x00FFFFFFu));
        }
      }
    }
    return;
  }

  double scale = (double)scale_pm / 1000.0;
  double inv_scale = 1.0 / scale;
  double rad = ((double)deg * 0.1) * (3.14159265358979323846 / 180.0);
  double cosA = cos(rad);
  double sinA = sin(rad);
  double absC = fabs(cosA);
  double absS = fabs(sinA);
  double cx = (double)w * 0.5;
  double cy = (double)h * 0.5;
  double dstCx = (double)x + cx;
  double dstCy = (double)y + cy;
  double halfW = (absC * (double)w + absS * (double)h) * 0.5 * scale;
  double halfH = (absS * (double)w + absC * (double)h) * 0.5 * scale;

  int ox0 = (int)floor(dstCx - halfW) - 1;
  int oy0 = (int)floor(dstCy - halfH) - 1;
  int ox1 = (int)ceil(dstCx + halfW) + 1;
  int oy1 = (int)ceil(dstCy + halfH) + 1;
  if (ox0 < d->clip_x0)
    ox0 = d->clip_x0;
  if (oy0 < d->clip_y0)
    oy0 = d->clip_y0;
  if (ox1 > d->clip_x1)
    ox1 = d->clip_x1;
  if (oy1 > d->clip_y1)
    oy1 = d->clip_y1;
  if (ox1 <= ox0 || oy1 <= oy0)
    return;

  region_read(s, 0, 0, w, h);
  region_write(d, ox0, oy0, ox1, oy1);

  double invA = cosA * inv_scale;
  double invB = sinA * inv_scale;
  double invC = -sinA * inv_scale;
  double invD = cosA * inv_scale;

  int64_t dsdx = (int64_t)llround(invA * 65536.0);
  int64_t dsdy = (int64_t)llround(invB * 65536.0);
  int64_t dtdx = (int64_t)llround(invC * 65536.0);
  int64_t dtdy = (int64_t)llround(invD * 65536.0);

  double startDx = (double)ox0 + 0.5 - dstCx;
  double startDy = (double)oy0 + 0.5 - dstCy;
  int64_t rowSx =
      (int64_t)llround((startDx * invA + startDy * invB + cx - 0.5) * 65536.0);
  int64_t rowSy =
      (int64_t)llround((startDx * invC + startDy * invD + cy - 0.5) * 65536.0);

  int sStride = s->stride;
  const u32 *srcPix = s->pixels;
  u32 opU = (u32)opacity_pm;

  for (int py = oy0; py < oy1; py++) {
    int64_t sxFp = rowSx;
    int64_t syFp = rowSy;
    rowSx += dsdy;
    rowSy += dtdy;
    u32 *dstRow = d->pixels + (size_t)py * (size_t)d->stride;
    for (int px = ox0; px < ox1; px++) {
      int ix = (int)((sxFp + 0x40000000LL) >> 16) - 16384;
      int iy = (int)((syFp + 0x40000000LL) >> 16) - 16384;
      int fx = (int)((sxFp - ((int64_t)ix << 16) + 128) >> 8);
      int fy = (int)((syFp - ((int64_t)iy << 16) + 128) >> 8);
      sxFp += dsdx;
      syFp += dtdx;
      if (ix < -1 || iy < -1 || ix >= w || iy >= h)
        continue;
      int omx = 256 - fx;
      int omy = 256 - fy;
      u32 w00 = (u32)(omx * omy) >> 4;
      u32 w10 = (u32)(fx * omy) >> 4;
      u32 w01 = (u32)(omx * fy) >> 4;
      u32 w11 = (u32)(fx * fy) >> 4;
      u32 sumA = 0, sumR = 0, sumG = 0, sumB = 0;

      if (ix >= 0 && iy >= 0) {
        u32 p = srcPix[(size_t)iy * (size_t)sStride + (size_t)ix];
        u32 a = p >> 24;
        if (a) {
          u32 wa = a * w00;
          sumA += wa;
          sumR += ((p >> 16) & 255u) * wa;
          sumG += ((p >> 8) & 255u) * wa;
          sumB += (p & 255u) * wa;
        }
      }
      if (ix + 1 >= 0 && ix + 1 < w && iy >= 0) {
        u32 p = srcPix[(size_t)iy * (size_t)sStride + (size_t)(ix + 1)];
        u32 a = p >> 24;
        if (a) {
          u32 wa = a * w10;
          sumA += wa;
          sumR += ((p >> 16) & 255u) * wa;
          sumG += ((p >> 8) & 255u) * wa;
          sumB += (p & 255u) * wa;
        }
      }
      if (ix >= 0 && iy + 1 >= 0 && iy + 1 < h) {
        u32 p = srcPix[(size_t)(iy + 1) * (size_t)sStride + (size_t)ix];
        u32 a = p >> 24;
        if (a) {
          u32 wa = a * w01;
          sumA += wa;
          sumR += ((p >> 16) & 255u) * wa;
          sumG += ((p >> 8) & 255u) * wa;
          sumB += (p & 255u) * wa;
        }
      }
      if (ix + 1 >= 0 && ix + 1 < w && iy + 1 >= 0 && iy + 1 < h) {
        u32 p = srcPix[(size_t)(iy + 1) * (size_t)sStride + (size_t)(ix + 1)];
        u32 a = p >> 24;
        if (a) {
          u32 wa = a * w11;
          sumA += wa;
          sumR += ((p >> 16) & 255u) * wa;
          sumG += ((p >> 8) & 255u) * wa;
          sumB += (p & 255u) * wa;
        }
      }
      if (sumA < 2048u)
        continue;
      u32 oa = (u32)(((uint64_t)sumA * opU + 2048000ULL) / 4096000ULL);
      if (oa == 0)
        continue;
      if (oa > 255u)
        oa = 255u;
      u32 halfA = sumA >> 1;
      u32 or_ = (sumR + halfA) / sumA;
      u32 og = (sumG + halfA) / sumA;
      u32 ob = (sumB + halfA) / sumA;
      if (or_ > 255u)
        or_ = 255u;
      if (og > 255u)
        og = 255u;
      if (ob > 255u)
        ob = 255u;
      u32 outPx = (oa << 24) | (or_ << 16) | (og << 8) | ob;
      dstRow[px] = (oa == 255u) ? outPx : blend_over(dstRow[px], outPx);
    }
  }
}

EXPORT void zan_gui_composite_surface(i32 dst_id, i32 src_id, i32 x, i32 y,
                                      i32 w, i32 h, i32 scale_pm,
                                      i32 deg_tenths, i32 opacity_pm) {
  if (dst_id < 0 || dst_id >= g_surface_count)
    return;
  if (src_id < 0 || src_id >= g_surface_count)
    return;
  zan_surface_t *d = g_surfaces[dst_id];
  zan_surface_t *s = g_surfaces[src_id];
  if (!d || !s || d == s || w <= 0 || h <= 0 || scale_pm <= 0 ||
      opacity_pm <= 0)
    return;
  if (d->be && d->be->composite_surface && s->be == d->be) {
    d->be->composite_surface(d, s, (int)x, (int)y, (int)w, (int)h,
                             (int)scale_pm, (int)deg_tenths, (int)opacity_pm);
    return;
  }
  if (s->be && s->be->sync_to_cpu)
    s->be->sync_to_cpu(s);
  if (d->be && d->be->sync_to_cpu)
    d->be->sync_to_cpu(d);
  cpu_composite_surface(d, s, (int)x, (int)y, (int)w, (int)h, (int)scale_pm,
                        (int)deg_tenths, (int)opacity_pm);
  if (d->be && d->be->sync_from_cpu)
    d->be->sync_from_cpu(d);
}
