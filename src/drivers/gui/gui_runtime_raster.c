/* gui_runtime_raster.c -- CPU 软件光栅化、模糊滤镜、渐变解算器与画板快照恢复 */

static void cpu_clear_rect(zan_surface_t *s, int x, int y, int w, int h,
                           u32 c) {
  int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
  int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
  int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
  int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
  region_write(s, x0, y0, x1, y1);
  long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
  if (area <= 0)
    return;
  ZAN_STAT(g_st_fill_op, area);
  stat_top_rect(1, (int)area, x0, y0, x1 - x0, y1 - y0, c);
  for (int py = y0; py < y1; py++) {
    fill_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
  }
}

EXPORT void zan_gui_clear_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                               i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, clear_rect)
      ->clear_rect(s, (int)x, (int)y, (int)w, (int)h, (u32)color);
}

static void cpu_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
  int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
  int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
  int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
  int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
  region_write(s, x0, y0, x1, y1);
  u32 sa = (c >> 24) & 0xFF;
  long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
  if (area < 0)
    area = 0;
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

EXPORT void zan_gui_fill_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                              i32 color) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_rect)
      ->fill_rect(s, (int)x, (int)y, (int)w, (int)h, (u32)color);
}

static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask);

static void cpu_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int corners, u32 ct, u32 cb) {
  int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
  int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
  int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
  int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
  int rw = x1 - x0, rh = y1 - y0;
  if (rw <= 0 || rh <= 0)
    return;
  region_write(s, x0, y0, x1, y1);
  int r = radius;
  int m = corners;
  if (r < 0)
    r = 0;
  if (r > 0 && (m & 15) == 0)
    r = 0;
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  int rh2 = h;
  if (rh2 < 1)
    rh2 = 1;
  int denom = rh2 > 1 ? rh2 - 1 : 1;
  ZAN_STAT(g_st_grad, (long long)rw * (long long)rh);
  int tr = (ct >> 16) & 0xFF, tg = (ct >> 8) & 0xFF, tb = ct & 0xFF;
  int mr = (cb >> 16) & 0xFF, mg = (cb >> 8) & 0xFF, mb = cb & 0xFF;
  int lx1 = r > 0 ? clamp_i(x + r, x0, x1) : x0;
  int rx0 = r > 0 ? clamp_i(x + w - r, lx1, x1) : x1;
  int mid_w = rx0 - lx1;
  for (int py = y0; py < y1; py++) {
    int num = py - y;
    if (num < 0)
      num = 0;
    if (num > rh2 - 1)
      num = rh2 - 1;
    int rr = tr + (mr - tr) * num / denom;
    int gg = tg + (mg - tg) * num / denom;
    int bb = tb + (mb - tb) * num / denom;
    u32 c = 0xFF000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
    u32 *row = s->pixels + py * s->stride;
    int j = py - y;
    if (r <= 0 || (j >= r && j < h - r)) {
      fill_run_const(row + x0, rw, c);
      continue;
    }
    for (int px = x0; px < lx1; px++) {
      int cov = zan_round_cov(px - x, j, w, h, r, m);
      if (cov >= 255)
        row[px] = c;
      else if (cov > 0)
        set_pixel_aa(s, px, py, c, cov);
    }
    if (mid_w > 0)
      fill_run_const(row + lx1, mid_w, c);
    for (int px = rx0; px < x1; px++) {
      int cov = zan_round_cov(px - x, j, w, h, r, m);
      if (cov >= 255)
        row[px] = c;
      else if (cov > 0)
        set_pixel_aa(s, px, py, c, cov);
    }
  }
}

EXPORT void zan_gui_fill_vgrad(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                               i32 color_top, i32 color_bottom) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_vgrad)
      ->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h, 0, ZAN_CORNERS_ALL,
                   (u32)color_top, (u32)color_bottom);
}

EXPORT void zan_gui_fill_vgrad_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                    i32 radius, i32 mask, i32 color_top,
                                    i32 color_bottom) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_vgrad)
      ->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h, (int)radius, (int)mask,
                   (u32)color_top, (u32)color_bottom);
}

static u32 grad_sample(u32 from, u32 via, u32 to, int t) {
  u32 a = from, b = to;
  if (via != 0) {
    if (t < 500) {
      b = via;
      t = t * 2;
    } else {
      a = via;
      t = (t - 500) * 2;
    }
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
                          int radius, int corners, int dir, u32 cf, u32 cv,
                          u32 ct) {
  int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
  int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
  int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
  int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
  if (x1 <= x0 || y1 <= y0)
    return;
  int r = radius;
  int m = corners;
  if (r < 0)
    r = 0;
  if (r > 0 && (m & 15) == 0)
    r = 0;
  region_write(s, x0, y0, x1, y1);
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  int d = dir;
  int span = (d == 1) ? w : h;
  if (d == 2 || d == 3)
    span = w + h - 1;
  if (span < 1)
    span = 1;
  int denom = span > 1 ? span - 1 : 1;
  ZAN_STAT(g_st_grad, (long long)(x1 - x0) * (long long)(y1 - y0));
  int lx1 = r > 0 ? clamp_i(x + r, x0, x1) : x0;
  int rx0 = r > 0 ? clamp_i(x + w - r, lx1, x1) : x1;
  int mid_w = rx0 - lx1;

  if (d == 0) {
    for (int py = y0; py < y1; py++) {
      u32 *row = s->pixels + py * s->stride;
      int j = py - y;
      u32 c = grad_sample(cf, cv, ct, clamp_i(j, 0, denom) * 1000 / denom);
      if ((c >> 24) == 0)
        continue;
      if (r <= 0 || (j >= r && j < h - r)) {
        blend_run_const(row + x0, x1 - x0, c);
        continue;
      }
      for (int px = x0; px < lx1; px++) {
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
      if (mid_w > 0)
        blend_run_const(row + lx1, mid_w, c);
      for (int px = rx0; px < x1; px++) {
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
    }
    return;
  }

  /* Precompute 1D gradient palette of length `span` once: O(w+h) instead of O(w*h). */
  static u32 *grad_lut = NULL;
  static int grad_lut_cap = 0;
  if (span > grad_lut_cap) {
    u32 *nl = (u32 *)realloc(grad_lut, (size_t)span * sizeof(u32));
    if (!nl)
      return;
    grad_lut = nl;
    grad_lut_cap = span;
  }
  int all_opaque = 1;
  for (int pos = 0; pos < span; pos++) {
    u32 col = grad_sample(cf, cv, ct, clamp_i(pos, 0, denom) * 1000 / denom);
    grad_lut[pos] = col;
    if ((col >> 24) != 255u)
      all_opaque = 0;
  }

  for (int py = y0; py < y1; py++) {
    u32 *row = s->pixels + py * s->stride;
    int j = py - y;
    int corner_row = (r > 0 && (j < r || j >= h - r));
    if (d == 1) {
      if (!corner_row) {
        const u32 *src = grad_lut + (x0 - x);
        int n = x1 - x0;
        if (all_opaque) {
          memcpy(row + x0, src, (size_t)n * sizeof(u32));
        } else {
          for (int k = 0; k < n; k++)
            row[x0 + k] = blend_over(row[x0 + k], src[k]);
        }
        continue;
      }
      for (int px = x0; px < lx1; px++) {
        u32 c = grad_lut[clamp_i(px - x, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
      if (mid_w > 0) {
        const u32 *src = grad_lut + (lx1 - x);
        if (all_opaque) {
          memcpy(row + lx1, src, (size_t)mid_w * sizeof(u32));
        } else {
          for (int k = 0; k < mid_w; k++)
            row[lx1 + k] = blend_over(row[lx1 + k], src[k]);
        }
      }
      for (int px = rx0; px < x1; px++) {
        u32 c = grad_lut[clamp_i(px - x, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
    } else if (d == 2) {
      int base = (x0 - x) + j;
      if (!corner_row) {
        int n = x1 - x0;
        const u32 *src = grad_lut + base;
        if (all_opaque) {
          memcpy(row + x0, src, (size_t)n * sizeof(u32));
        } else {
          for (int k = 0; k < n; k++)
            row[x0 + k] = blend_over(row[x0 + k], src[k]);
        }
        continue;
      }
      for (int px = x0; px < lx1; px++) {
        u32 c = grad_lut[clamp_i((px - x) + j, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
      if (mid_w > 0) {
        const u32 *src = grad_lut + (lx1 - x) + j;
        if (all_opaque) {
          memcpy(row + lx1, src, (size_t)mid_w * sizeof(u32));
        } else {
          for (int k = 0; k < mid_w; k++)
            row[lx1 + k] = blend_over(row[lx1 + k], src[k]);
        }
      }
      for (int px = rx0; px < x1; px++) {
        u32 c = grad_lut[clamp_i((px - x) + j, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
    } else {
      /* dir == 3: pos = (w - 1 - (px - x)) + j, decreasing by 1 per px */
      int pos0 = (w - 1 - (x0 - x)) + j;
      if (!corner_row) {
        int n = x1 - x0;
        if (all_opaque) {
          for (int k = 0; k < n; k++)
            row[x0 + k] = grad_lut[pos0 - k];
        } else {
          for (int k = 0; k < n; k++)
            row[x0 + k] = blend_over(row[x0 + k], grad_lut[pos0 - k]);
        }
        continue;
      }
      for (int px = x0; px < lx1; px++) {
        u32 c = grad_lut[clamp_i((w - 1 - (px - x)) + j, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
      if (mid_w > 0) {
        int mpos0 = (w - 1 - (lx1 - x)) + j;
        if (all_opaque) {
          for (int k = 0; k < mid_w; k++)
            row[lx1 + k] = grad_lut[mpos0 - k];
        } else {
          for (int k = 0; k < mid_w; k++)
            row[lx1 + k] = blend_over(row[lx1 + k], grad_lut[mpos0 - k]);
        }
      }
      for (int px = rx0; px < x1; px++) {
        u32 c = grad_lut[clamp_i((w - 1 - (px - x)) + j, 0, span - 1)];
        int cov = zan_round_cov(px - x, j, w, h, r, m);
        if (cov >= 255)
          row[px] = all_opaque ? c : blend_over(row[px], c);
        else if (cov > 0)
          set_pixel_aa(s, px, py, c, cov);
      }
    }
  }
}

EXPORT void zan_gui_fill_grad_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                   i32 radius, i32 mask, i32 dir,
                                   i32 color_from, i32 color_via,
                                   i32 color_to) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, fill_grad)
      ->fill_grad(s, (int)x, (int)y, (int)w, (int)h, (int)radius, (int)mask,
                  (int)dir, (u32)color_from, (u32)color_via, (u32)color_to);
}

/* 抗锯齿圆角矩形掩模采样与混合 */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask) {
  if (cr <= 0)
    return 255;
  if (cr > rw / 2)
    cr = rw / 2;
  if (cr > rh / 2)
    cr = rh / 2;
  if (cr <= 0)
    return 255;
  if ((i >= cr && i < rw - cr) || (j >= cr && j < rh - cr))
    return 255;
  int cx, cy, bit;
  if (i < cr && j < cr) {
    cx = cr;
    cy = cr;
    bit = 1;
  } else if (i >= rw - cr && j < cr) {
    cx = rw - cr;
    cy = cr;
    bit = 2;
  } else if (i < cr && j >= rh - cr) {
    cx = cr;
    cy = rh - cr;
    bit = 8;
  } else {
    cx = rw - cr;
    cy = rh - cr;
    bit = 4;
  }
  if (!(cmask & bit))
    return 255; /* square (welded) corner */
  double dx = (double)i + 0.5 - (double)cx;
  double dy = (double)j + 0.5 - (double)cy;
  double dist = sqrt(dx * dx + dy * dy);
  double cov = (double)cr - dist + 0.5;
  if (cov >= 1.0)
    return 255;
  if (cov <= 0.0)
    return 0;
  return (int)(cov * 255.0);
}

static void zan_blur_rect_core(zan_surface_t *s, int x, int y, int w, int h,
                               int radius, int cr, int cmask, u32 *capture) {
  int x0 = clamp_i(x, 0, s->width);
  int y0 = clamp_i(y, 0, s->height);
  int x1 = clamp_i(x + w, 0, s->width);
  int y1 = clamp_i(y + h, 0, s->height);
  int rw = x1 - x0, rh = y1 - y0;
  if (rw <= 0 || rh <= 0)
    return;
  if (!clip_hits(s, x0, y0, x1, y1))
    return;
  region_read(s, x0, y0, x1, y1);
  region_write(s, x0, y0, x1, y1);
  int r = radius;
  if (r < 1)
    return;
  if (r > 40)
    r = 40;

  ZAN_STAT(g_st_blur, (long long)rw * (long long)rh);
  int ds = 1;
  if (r >= 12) {
    ds = 4;
  } else if (r >= 6) {
    ds = 2;
  }
  int dw = (rw + ds - 1) / ds;
  int dh = (rh + ds - 1) / ds;
  if (dw < 1)
    dw = 1;
  if (dh < 1)
    dh = 1;
  size_t dn = (size_t)dw * (size_t)dh;
  static u32 *scratch_a = NULL, *scratch_b = NULL;
  static size_t scratch_n = 0;
  if (dn > scratch_n) {
    u32 *na = (u32 *)realloc(scratch_a, dn * sizeof(u32));
    if (!na)
      return;
    scratch_a = na;
    u32 *nb = (u32 *)realloc(scratch_b, dn * sizeof(u32));
    if (!nb)
      return;
    scratch_b = nb;
    scratch_n = dn;
  }
  u32 *a = scratch_a;
  u32 *b = scratch_b;

  /* 双线性/块均值下采样缩放 */
  if (ds == 1) {
    for (int dj = 0; dj < dh; dj++) {
      const u32 *row = s->pixels + (y0 + dj) * s->stride + x0;
      u32 *dst = a + dj * dw;
      for (int di = 0; di < dw; di++)
        dst[di] = row[di] | 0xFF000000u;
    }
  } else {
    int shift = (ds == 2) ? 2 : 4;
    for (int dj = 0; dj < dh; dj++) {
      int sy0 = dj * ds;
      int sy1 = sy0 + ds;
      int full_y = (sy1 <= rh);
      if (!full_y)
        sy1 = rh;
      u32 *dst = a + dj * dw;
      if (full_y && ds == 2) {
        const u32 *r0 = s->pixels + (y0 + sy0) * s->stride + x0;
        const u32 *r1 = r0 + s->stride;
        int full_dw = rw >> 1;
        for (int di = 0; di < full_dw; di++) {
          int sx = di << 1;
          u32 p0 = r0[sx], p1 = r0[sx + 1], p2 = r1[sx], p3 = r1[sx + 1];
          u32 sr = ((p0 >> 16) & 0xFFu) + ((p1 >> 16) & 0xFFu) +
                   ((p2 >> 16) & 0xFFu) + ((p3 >> 16) & 0xFFu);
          u32 sg = ((p0 >> 8) & 0xFFu) + ((p1 >> 8) & 0xFFu) +
                   ((p2 >> 8) & 0xFFu) + ((p3 >> 8) & 0xFFu);
          u32 sb = (p0 & 0xFFu) + (p1 & 0xFFu) + (p2 & 0xFFu) + (p3 & 0xFFu);
          dst[di] = 0xFF000000u | ((sr >> 2) << 16) | ((sg >> 2) << 8) | (sb >> 2);
        }
        for (int di = full_dw; di < dw; di++) {
          int sx0 = di * ds;
          int sx1 = sx0 + ds;
          if (sx1 > rw) sx1 = rw;
          int sr = 0, sg = 0, sb = 0, cnt = 0;
          for (int yy = sy0; yy < sy1; yy++) {
            const u32 *row = s->pixels + (y0 + yy) * s->stride + x0;
            for (int xx = sx0; xx < sx1; xx++) {
              u32 px = row[xx];
              sr += (px >> 16) & 0xFF;
              sg += (px >> 8) & 0xFF;
              sb += px & 0xFF;
              cnt++;
            }
          }
          if (cnt < 1) cnt = 1;
          dst[di] = 0xFF000000u | ((u32)(sr / cnt) << 16) |
                    ((u32)(sg / cnt) << 8) | (u32)(sb / cnt);
        }
      } else if (full_y && ds == 4) {
        const u32 *r0 = s->pixels + (y0 + sy0) * s->stride + x0;
        const u32 *r1 = r0 + s->stride;
        const u32 *r2 = r1 + s->stride;
        const u32 *r3 = r2 + s->stride;
        int full_dw = rw >> 2;
        int di = 0;
#if ZAN_HAS_SSE2
        const __m128i vz = _mm_setzero_si128();
        for (; di < full_dw; di++) {
          int sx = di << 2;
          __m128i v0 = _mm_loadu_si128((const __m128i *)(r0 + sx));
          __m128i v1 = _mm_loadu_si128((const __m128i *)(r1 + sx));
          __m128i v2 = _mm_loadu_si128((const __m128i *)(r2 + sx));
          __m128i v3 = _mm_loadu_si128((const __m128i *)(r3 + sx));
          __m128i sum = _mm_add_epi16(
              _mm_add_epi16(_mm_unpacklo_epi8(v0, vz), _mm_unpackhi_epi8(v0, vz)),
              _mm_add_epi16(_mm_unpacklo_epi8(v1, vz), _mm_unpackhi_epi8(v1, vz)));
          sum = _mm_add_epi16(
              sum,
              _mm_add_epi16(
                  _mm_add_epi16(_mm_unpacklo_epi8(v2, vz), _mm_unpackhi_epi8(v2, vz)),
                  _mm_add_epi16(_mm_unpacklo_epi8(v3, vz), _mm_unpackhi_epi8(v3, vz))));
          sum = _mm_add_epi16(sum, _mm_srli_si128(sum, 8));
          sum = _mm_srli_epi16(sum, 4);
          __m128i pk = _mm_packus_epi16(sum, vz);
          dst[di] = (u32)_mm_cvtsi128_si32(pk) | 0xFF000000u;
        }
#else
        for (; di < full_dw; di++) {
          int sx = di << 2;
          u32 sr = 0, sg = 0, sb = 0;
          for (int k = 0; k < 4; k++) {
            u32 p0 = r0[sx + k], p1 = r1[sx + k], p2 = r2[sx + k], p3 = r3[sx + k];
            sr += ((p0 >> 16) & 0xFFu) + ((p1 >> 16) & 0xFFu) +
                  ((p2 >> 16) & 0xFFu) + ((p3 >> 16) & 0xFFu);
            sg += ((p0 >> 8) & 0xFFu) + ((p1 >> 8) & 0xFFu) +
                  ((p2 >> 8) & 0xFFu) + ((p3 >> 8) & 0xFFu);
            sb += (p0 & 0xFFu) + (p1 & 0xFFu) + (p2 & 0xFFu) + (p3 & 0xFFu);
          }
          dst[di] = 0xFF000000u | ((sr >> 4) << 16) | ((sg >> 4) << 8) | (sb >> 4);
        }
#endif
        for (; di < dw; di++) {
          int sx0 = di * ds;
          int sx1 = sx0 + ds;
          if (sx1 > rw)
            sx1 = rw;
          int sr = 0, sg = 0, sb = 0, cnt = 0;
          for (int yy = sy0; yy < sy1; yy++) {
            const u32 *row = s->pixels + (y0 + yy) * s->stride + x0;
            for (int xx = sx0; xx < sx1; xx++) {
              u32 px = row[xx];
              sr += (px >> 16) & 0xFF;
              sg += (px >> 8) & 0xFF;
              sb += px & 0xFF;
              cnt++;
            }
          }
          if (cnt < 1)
            cnt = 1;
          dst[di] = 0xFF000000u | ((u32)(sr / cnt) << 16) |
                    ((u32)(sg / cnt) << 8) | (u32)(sb / cnt);
        }
      } else {
        int full_dw = full_y ? (rw / ds) : 0;
        for (int di = 0; di < full_dw; di++) {
          int sx0 = di * ds;
          u32 sr = 0, sg = 0, sb = 0;
          for (int yy = sy0; yy < sy1; yy++) {
            const u32 *row = s->pixels + (y0 + yy) * s->stride + x0 + sx0;
            for (int k = 0; k < ds; k++) {
              u32 px = row[k];
              sr += (px >> 16) & 0xFFu;
              sg += (px >> 8) & 0xFFu;
              sb += px & 0xFFu;
            }
          }
          dst[di] = 0xFF000000u | ((sr >> shift) << 16) |
                    ((sg >> shift) << 8) | (sb >> shift);
        }
        for (int di = full_dw; di < dw; di++) {
          int sx0 = di * ds;
          int sx1 = sx0 + ds;
          if (sx1 > rw)
            sx1 = rw;
          int sr = 0, sg = 0, sb = 0, cnt = 0;
          for (int yy = sy0; yy < sy1; yy++) {
            const u32 *row = s->pixels + (y0 + yy) * s->stride + x0;
            for (int xx = sx0; xx < sx1; xx++) {
              u32 px = row[xx];
              sr += (px >> 16) & 0xFF;
              sg += (px >> 8) & 0xFF;
              sb += px & 0xFF;
              cnt++;
            }
          }
          if (cnt < 1)
            cnt = 1;
          dst[di] = 0xFF000000u | ((u32)(sr / cnt) << 16) |
                    ((u32)(sg / cnt) << 8) | (u32)(sb / cnt);
        }
      }
    }
  }

  int rr = r / ds;
  if (rr < 1)
    rr = 1;
  int win = 2 * rr + 1;
  /* Exact 21-bit fixed-point reciprocal: (sum * inv_win) >> 21 == sum / win
   * for all win in [1..81] and sum in [0..255*win] without 32-bit overflow. */
  u32 inv_win = ((1u << 21) + (u32)win - 1u) / (u32)win;

  static int *col_sums = NULL;
  static int col_sums_cap = 0;
  if (dw > col_sums_cap) {
    int *nc = (int *)realloc(col_sums, (size_t)dw * 3u * sizeof(int));
    if (!nc)
      return;
    col_sums = nc;
    col_sums_cap = dw;
  }

  for (int p = 0; p < 2; p++) {
    /* horizontal: a -> b (branchless interior, exact reciprocal) */
    int mid_end = dw - rr - 1;
    for (int j = 0; j < dh; j++) {
      const u32 *src = a + j * dw;
      u32 *dst = b + j * dw;
      int sr = 0, sg = 0, sb = 0;
      for (int k = -rr; k <= rr; k++) {
        u32 px = src[clamp_i(k, 0, dw - 1)];
        sr += (px >> 16) & 0xFF;
        sg += (px >> 8) & 0xFF;
        sb += px & 0xFF;
      }
      if (rr < mid_end) {
        for (int i = 0; i < rr; i++) {
          dst[i] = 0xFF000000u | ((((u32)sr * inv_win) >> 21) << 16) |
                   ((((u32)sg * inv_win) >> 21) << 8) |
                   (((u32)sb * inv_win) >> 21);
          u32 pa = src[i + rr + 1];
          u32 ps = src[0];
          sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
          sg += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
          sb += (int)(pa & 0xFF) - (int)(ps & 0xFF);
        }
        for (int i = rr; i < mid_end; i++) {
          dst[i] = 0xFF000000u | ((((u32)sr * inv_win) >> 21) << 16) |
                   ((((u32)sg * inv_win) >> 21) << 8) |
                   (((u32)sb * inv_win) >> 21);
          u32 pa = src[i + rr + 1];
          u32 ps = src[i - rr];
          sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
          sg += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
          sb += (int)(pa & 0xFF) - (int)(ps & 0xFF);
        }
        for (int i = mid_end; i < dw; i++) {
          dst[i] = 0xFF000000u | ((((u32)sr * inv_win) >> 21) << 16) |
                   ((((u32)sg * inv_win) >> 21) << 8) |
                   (((u32)sb * inv_win) >> 21);
          u32 pa = src[clamp_i(i + rr + 1, 0, dw - 1)];
          u32 ps = src[clamp_i(i - rr, 0, dw - 1)];
          sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
          sg += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
          sb += (int)(pa & 0xFF) - (int)(ps & 0xFF);
        }
      } else {
        for (int i = 0; i < dw; i++) {
          dst[i] = 0xFF000000u | ((((u32)sr * inv_win) >> 21) << 16) |
                   ((((u32)sg * inv_win) >> 21) << 8) |
                   (((u32)sb * inv_win) >> 21);
          u32 pa = src[clamp_i(i + rr + 1, 0, dw - 1)];
          u32 ps = src[clamp_i(i - rr, 0, dw - 1)];
          sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
          sg += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
          sb += (int)(pa & 0xFF) - (int)(ps & 0xFF);
        }
      }
    }
    /* vertical: b -> a (cache-friendly row-major running column sums) */
    {
      const u32 *r0 = b;
      int mult0 = rr + 1;
      for (int i = 0; i < dw; i++) {
        u32 px = r0[i];
        col_sums[3 * i + 0] = (int)((px >> 16) & 0xFFu) * mult0;
        col_sums[3 * i + 1] = (int)((px >> 8) & 0xFFu) * mult0;
        col_sums[3 * i + 2] = (int)(px & 0xFFu) * mult0;
      }
      for (int k = 1; k <= rr; k++) {
        const u32 *rk = b + clamp_i(k, 0, dh - 1) * dw;
        for (int i = 0; i < dw; i++) {
          u32 px = rk[i];
          col_sums[3 * i + 0] += (int)((px >> 16) & 0xFFu);
          col_sums[3 * i + 1] += (int)((px >> 8) & 0xFFu);
          col_sums[3 * i + 2] += (int)(px & 0xFFu);
        }
      }
      for (int j = 0; j < dh; j++) {
        u32 *dst = a + j * dw;
        const u32 *row_add = b + clamp_i(j + rr + 1, 0, dh - 1) * dw;
        const u32 *row_sub = b + clamp_i(j - rr, 0, dh - 1) * dw;
        for (int i = 0; i < dw; i++) {
          int sr = col_sums[3 * i + 0];
          int sg = col_sums[3 * i + 1];
          int sb = col_sums[3 * i + 2];
          dst[i] = 0xFF000000u | ((((u32)sr * inv_win) >> 21) << 16) |
                   ((((u32)sg * inv_win) >> 21) << 8) |
                   (((u32)sb * inv_win) >> 21);
          u32 pa = row_add[i];
          u32 ps = row_sub[i];
          col_sums[3 * i + 0] = sr + (int)((pa >> 16) & 0xFFu) - (int)((ps >> 16) & 0xFFu);
          col_sums[3 * i + 1] = sg + (int)((pa >> 8) & 0xFFu) - (int)((ps >> 8) & 0xFFu);
          col_sums[3 * i + 2] = sb + (int)(pa & 0xFFu) - (int)(ps & 0xFFu);
        }
      }
    }
  }

  /* Vibrancy (luma-preserving ~1.6x) */
  for (int i = 0; i < (int)dn; i++) {
    u32 px = a[i];
    int rc = (int)((px >> 16) & 0xFF);
    int gc = (int)((px >> 8) & 0xFF);
    int bc = (int)(px & 0xFF);
    int luma = (rc * 77 + gc * 150 + bc * 29) >> 8;
    rc = luma + (rc - luma) * 8 / 5;
    gc = luma + (gc - luma) * 8 / 5;
    bc = luma + (bc - luma) * 8 / 5;
    a[i] = 0xFF000000u | ((u32)clamp_i(rc, 0, 255) << 16) |
           ((u32)clamp_i(gc, 0, 255) << 8) | (u32)clamp_i(bc, 0, 255);
  }

  int ci0 = clamp_i(s->clip_x0 - x0, 0, rw);
  int ci1 = clamp_i(s->clip_x1 - x0, 0, rw);
  int cj0 = clamp_i(s->clip_y0 - y0, 0, rh);
  int cj1 = clamp_i(s->clip_y1 - y0, 0, rh);
  int eff_cr = cr;
  if (eff_cr > rw / 2) eff_cr = rw / 2;
  if (eff_cr > rh / 2) eff_cr = rh / 2;
  if (eff_cr < 0) eff_cr = 0;

  if (ds == 1) {
    for (int j = cj0; j < cj1; j++) {
      const u32 *src = a + j * dw;
      u32 *dst_row = s->pixels + (y0 + j) * s->stride + x0;
      int corner_row = (eff_cr > 0 && (j < eff_cr || j >= rh - eff_cr));
      int y_term = (y0 + j) * 173;
      for (int i = ci0; i < ci1; i++) {
        u32 px = src[i];
        int nz = ((((x0 + i) * 197 + y_term) >> 3) % 7) - 3;
        int rC = clamp_i((int)((px >> 16) & 0xFF) + nz, 0, 255);
        int gC = clamp_i((int)((px >> 8) & 0xFF) + nz, 0, 255);
        int bC = clamp_i((int)(px & 0xFF) + nz, 0, 255);
        u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
        if (corner_row && (i < eff_cr || i >= rw - eff_cr)) {
          int cov = zan_round_cov(i, j, rw, rh, eff_cr, cmask);
          if (cov >= 255)
            dst_row[i] = out;
          else if (cov > 0)
            set_pixel_aa(s, x0 + i, y0 + j, out, cov);
        } else {
          dst_row[i] = out;
        }
      }
    }
  } else {
    typedef struct { int gx, gx1, wx; } zan_blur_xlut_t;
    static zan_blur_xlut_t *xlut = NULL;
    static int xlut_cap = 0;
    if (rw > xlut_cap) {
      zan_blur_xlut_t *nx = (zan_blur_xlut_t *)realloc(xlut, (size_t)rw * sizeof(*xlut));
      if (!nx)
        return;
      xlut = nx;
      xlut_cap = rw;
    }
    int inv_ds2 = 256 / (ds * 2); /* exact for ds == 2 (64) and ds == 4 (32) */
    for (int i = ci0; i < ci1; i++) {
      int fx = (i * 2 + 1) * inv_ds2 - 128;
      if (fx < 0) fx = 0;
      int gx = fx >> 8;
      int wx = fx & 255;
      if (gx > dw - 1) { gx = dw - 1; wx = 0; }
      int gx1 = gx + 1;
      if (gx1 > dw - 1) gx1 = dw - 1;
      xlut[i].gx = gx;
      xlut[i].gx1 = gx1;
      xlut[i].wx = wx;
    }
    for (int j = cj0; j < cj1; j++) {
      int fy = (j * 2 + 1) * inv_ds2 - 128;
      if (fy < 0)
        fy = 0;
      int gy = fy >> 8;
      int wy = fy & 255;
      if (gy > dh - 1) {
        gy = dh - 1;
        wy = 0;
      }
      int gy1 = gy + 1;
      if (gy1 > dh - 1)
        gy1 = dh - 1;
      const u32 *row0 = a + gy * dw;
      const u32 *row1 = a + gy1 * dw;
      u32 *dst_row = s->pixels + (y0 + j) * s->stride + x0;
      int iwy = 256 - wy;
      int corner_row = (eff_cr > 0 && (j < eff_cr || j >= rh - eff_cr));
      int y_term = (y0 + j) * 173;
      if (!corner_row) {
#if ZAN_HAS_SSE2
        const __m128i vz = _mm_setzero_si128();
        const __m128i v_wy = _mm_set1_epi16((short)wy);
        const __m128i v_iwy = _mm_set1_epi16((short)iwy);
        const __m128i v_256 = _mm_set1_epi16(256);
        int i = ci0;
        for (; i + 1 < ci1; i += 2) {
          int gx_a = xlut[i].gx, gx1_a = xlut[i].gx1, wx_a = xlut[i].wx;
          int gx_b = xlut[i + 1].gx, gx1_b = xlut[i + 1].gx1, wx_b = xlut[i + 1].wx;
          /* 垂直线性插值后右移 8 位，再做水平线性插值并右移 8 位（与原 >>16 精度对齐）。 */
          __m128i r0_0 = _mm_unpacklo_epi8(
              _mm_setr_epi32((int)row0[gx_a], (int)row0[gx_b], 0, 0), vz);
          __m128i r1_0 = _mm_unpacklo_epi8(
              _mm_setr_epi32((int)row1[gx_a], (int)row1[gx_b], 0, 0), vz);
          __m128i r0_1 = _mm_unpacklo_epi8(
              _mm_setr_epi32((int)row0[gx1_a], (int)row0[gx1_b], 0, 0), vz);
          __m128i r1_1 = _mm_unpacklo_epi8(
              _mm_setr_epi32((int)row1[gx1_a], (int)row1[gx1_b], 0, 0), vz);
          __m128i col0 = _mm_srli_epi16(
              _mm_add_epi16(_mm_mullo_epi16(r0_0, v_iwy), _mm_mullo_epi16(r1_0, v_wy)),
              8);
          __m128i col1 = _mm_srli_epi16(
              _mm_add_epi16(_mm_mullo_epi16(r0_1, v_iwy), _mm_mullo_epi16(r1_1, v_wy)),
              8);
          __m128i v_wx = _mm_setr_epi16((short)wx_a, (short)wx_a, (short)wx_a, 0,
                                        (short)wx_b, (short)wx_b, (short)wx_b, 0);
          __m128i v_iwx = _mm_sub_epi16(v_256, v_wx);
          __m128i pix = _mm_srli_epi16(
              _mm_add_epi16(_mm_mullo_epi16(col0, v_iwx), _mm_mullo_epi16(col1, v_wx)),
              8);
          u32 u0 = (u32)(((x0 + i) * 197 + y_term) >> 3);
          u32 u1 = (u32)(((x0 + i + 1) * 197 + y_term) >> 3);
          int nz0 = (int)(u0 - (u32)(((uint64_t)u0 * 613566757ULL) >> 32) * 7u) - 3;
          int nz1 = (int)(u1 - (u32)(((uint64_t)u1 * 613566757ULL) >> 32) * 7u) - 3;
          __m128i v_nz = _mm_setr_epi16((short)nz0, (short)nz0, (short)nz0, 0,
                                        (short)nz1, (short)nz1, (short)nz1, 0);
          pix = _mm_add_epi16(pix, v_nz);
          __m128i packed = _mm_packus_epi16(pix, vz);
          u32 out0 = (u32)_mm_cvtsi128_si32(packed) | 0xFF000000u;
          u32 out1 = (u32)_mm_cvtsi128_si32(_mm_srli_si128(packed, 4)) | 0xFF000000u;
          dst_row[i] = out0;
          dst_row[i + 1] = out1;
        }
        for (; i < ci1; i++) {
          int gx = xlut[i].gx, gx1 = xlut[i].gx1, wx = xlut[i].wx;
          u32 p00 = row0[gx], p01 = row0[gx1], p10 = row1[gx], p11 = row1[gx1];
          int iwx = 256 - wx;
          int w00 = iwx * iwy, w01 = wx * iwy, w10 = iwx * wy, w11 = wx * wy;
          int rC = ((int)((p00 >> 16) & 0xFF) * w00 + (int)((p01 >> 16) & 0xFF) * w01 +
                    (int)((p10 >> 16) & 0xFF) * w10 + (int)((p11 >> 16) & 0xFF) * w11) >> 16;
          int gC = ((int)((p00 >> 8) & 0xFF) * w00 + (int)((p01 >> 8) & 0xFF) * w01 +
                    (int)((p10 >> 8) & 0xFF) * w10 + (int)((p11 >> 8) & 0xFF) * w11) >> 16;
          int bC = ((int)(p00 & 0xFF) * w00 + (int)(p01 & 0xFF) * w01 +
                    (int)(p10 & 0xFF) * w10 + (int)(p11 & 0xFF) * w11) >> 16;
          u32 u = (u32)(((x0 + i) * 197 + y_term) >> 3);
          int nz = (int)(u - (u32)(((uint64_t)u * 613566757ULL) >> 32) * 7u) - 3;
          rC = clamp_i(rC + nz, 0, 255);
          gC = clamp_i(gC + nz, 0, 255);
          bC = clamp_i(bC + nz, 0, 255);
          dst_row[i] = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
        }
#else
        for (int i = ci0; i < ci1; i++) {
          int gx = xlut[i].gx, gx1 = xlut[i].gx1, wx = xlut[i].wx;
          u32 p00 = row0[gx], p01 = row0[gx1], p10 = row1[gx], p11 = row1[gx1];
          int iwx = 256 - wx;
          int w00 = iwx * iwy, w01 = wx * iwy, w10 = iwx * wy, w11 = wx * wy;
          int rC = ((int)((p00 >> 16) & 0xFF) * w00 + (int)((p01 >> 16) & 0xFF) * w01 +
                    (int)((p10 >> 16) & 0xFF) * w10 + (int)((p11 >> 16) & 0xFF) * w11) >> 16;
          int gC = ((int)((p00 >> 8) & 0xFF) * w00 + (int)((p01 >> 8) & 0xFF) * w01 +
                    (int)((p10 >> 8) & 0xFF) * w10 + (int)((p11 >> 8) & 0xFF) * w11) >> 16;
          int bC = ((int)(p00 & 0xFF) * w00 + (int)(p01 & 0xFF) * w01 +
                    (int)(p10 & 0xFF) * w10 + (int)(p11 & 0xFF) * w11) >> 16;
          u32 u = (u32)(((x0 + i) * 197 + y_term) >> 3);
          int nz = (int)(u - (u32)(((uint64_t)u * 613566757ULL) >> 32) * 7u) - 3;
          rC = clamp_i(rC + nz, 0, 255);
          gC = clamp_i(gC + nz, 0, 255);
          bC = clamp_i(bC + nz, 0, 255);
          dst_row[i] = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
        }
#endif
        continue;
      }
      for (int i = ci0; i < ci1; i++) {
        int gx = xlut[i].gx;
        int gx1 = xlut[i].gx1;
        int wx = xlut[i].wx;
        u32 p00 = row0[gx];
        u32 p01 = row0[gx1];
        u32 p10 = row1[gx];
        u32 p11 = row1[gx1];
        int iwx = 256 - wx;
        int w00 = iwx * iwy;
        int w01 = wx * iwy;
        int w10 = iwx * wy;
        int w11 = wx * wy;
        int rC =
            ((int)((p00 >> 16) & 0xFF) * w00 + (int)((p01 >> 16) & 0xFF) * w01 +
             (int)((p10 >> 16) & 0xFF) * w10 +
             (int)((p11 >> 16) & 0xFF) * w11) >>
            16;
        int gC =
            ((int)((p00 >> 8) & 0xFF) * w00 + (int)((p01 >> 8) & 0xFF) * w01 +
             (int)((p10 >> 8) & 0xFF) * w10 + (int)((p11 >> 8) & 0xFF) * w11) >>
            16;
        int bC = ((int)(p00 & 0xFF) * w00 + (int)(p01 & 0xFF) * w01 +
                  (int)(p10 & 0xFF) * w10 + (int)(p11 & 0xFF) * w11) >>
                 16;
        u32 u = (u32)(((x0 + i) * 197 + y_term) >> 3);
        int nz = (int)(u - (u32)(((uint64_t)u * 613566757ULL) >> 32) * 7u) - 3;
        rC = clamp_i(rC + nz, 0, 255);
        gC = clamp_i(gC + nz, 0, 255);
        bC = clamp_i(bC + nz, 0, 255);
        u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
        if (i < eff_cr || i >= rw - eff_cr) {
          int cov = zan_round_cov(i, j, rw, rh, eff_cr, cmask);
          if (cov >= 255)
            dst_row[i] = out;
          else if (cov > 0)
            set_pixel_aa(s, x0 + i, y0 + j, out, cov);
        } else {
          dst_row[i] = out;
        }
      }
    }
  }
  if (capture) {
    for (int j = 0; j < rh; j++) {
      memcpy(capture + (size_t)j * (size_t)rw,
             s->pixels + (y0 + j) * s->stride + x0,
             (size_t)rw * sizeof(u32));
    }
  }
}

EXPORT void zan_gui_blur_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                              i32 radius) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius, -1, 1,
                          0, ZAN_CORNERS_ALL);
}

#define ZAN_BLUR_CACHE_SLOTS 256
/* Final glass composition belongs to the same bounded blur slot. */
typedef struct {
  int valid, pending, sid;
  int key[12];
  u32 *pixels; /* source followed by final straight-ARGB pixels */
  size_t cap;
  uint64_t age;
} zan_glass_cache_t;

typedef struct {
  int valid;
  int sid;
  int x0, y0, rw, rh, r;
  int cr, cmask;
  u32 src_sum;
  u32 *pixels;
  size_t cap;
  zan_glass_cache_t glass;
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

#include "gui_runtime_glass.c"

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
  if (rw <= 0 || rh <= 0)
    return;
  if (!clip_hits(s, x0, y0, x1, y1))
    return;
  region_read(s, x0, y0, x1, y1);
  region_write(s, x0, y0, x1, y1);
  int r = radius;
  if (r < 1)
    return;
  if (r > 40)
    r = 40;

  zan_blur_cache_t *c = &g_blur_cache[slot];
  /* Any independent blur call changes this slot's output dependency. */
  if (!c->glass.pending)
    c->glass.valid = 0;
  size_t n = (size_t)rw * (size_t)rh;

  int geom_ok = c->valid && c->pixels && c->sid == s->id && c->x0 == x0 &&
                c->y0 == y0 && c->rw == rw && c->rh == rh && c->r == r &&
                c->cr == cr && c->cmask == cmask;
  int covers = clip_covers(s, x0, y0, x1, y1);
  u32 sum = geom_ok && dirty ? zan_src_sum(s, x0, y0, rw, rh) : 0;
  if (geom_ok && (!dirty || (covers && sum == c->src_sum))) {
    /* Keep the established panel-area blurhit statistic. Cached pixels
     * already contain rounded coverage and straight alpha: copy, no blend. */
    ZAN_STAT(g_st_blur_hit, (long long)rw * (long long)rh);
    int cx0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
    int cy0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
    int cx1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
    int cy1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
    for (int py = cy0; py < cy1; py++) {
      memcpy(s->pixels + (size_t)py * s->stride + cx0,
             c->pixels + (size_t)(py - y0) * rw + (cx0 - x0),
             (size_t)(cx1 - cx0) * sizeof(u32));
    }
    return;
  }

  u32 fresh = geom_ok ? sum : zan_src_sum(s, x0, y0, rw, rh);
  if (c->cap < n) {
    u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
    if (np) {
      c->pixels = np;
      c->cap = n;
    }
  }
  zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask,
                     c->cap >= n ? c->pixels : NULL);
  if (!covers) {
    g_blur_partial_miss++;
  }
  if (!covers || c->cap < n) {
    c->valid = 0;
    return;
  }
  c->x0 = x0;
  c->y0 = y0;
  c->rw = rw;
  c->rh = rh;
  c->r = r;
  c->cr = cr;
  c->cmask = cmask;
  c->src_sum = fresh;
  c->sid = s->id;
  c->valid = 1;
}

EXPORT i32 zan_gui_blur_partial_miss_take(void) {
  i32 v = (i32)g_blur_partial_miss;
  g_blur_partial_miss = 0;
  return v;
}

EXPORT void zan_gui_blur_rect_cached(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                     i32 radius, i32 slot, i32 dirty) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                          (int)slot, (int)dirty, 0, ZAN_CORNERS_ALL);
}

EXPORT void zan_gui_blur_round_cached(i32 surface_id, i32 x, i32 y, i32 w,
                                      i32 h, i32 radius, i32 slot, i32 dirty,
                                      i32 corner_radius, i32 corner_mask) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
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
  if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS)
    return;
  if (slot < g_snap_cache_count)
    return;
  int grow = slot + 1;
  if (grow < ZAN_SNAP_CACHE_SLOTS)
    grow = ZAN_SNAP_CACHE_SLOTS;
  zan_blur_cache_t *np = (zan_blur_cache_t *)realloc(
      g_snap_cache, (size_t)grow * sizeof(zan_blur_cache_t));
  if (!np)
    return;
  for (int i = g_snap_cache_count; i < grow; i++) {
    np[i].valid = 0;
    np[i].pixels = NULL;
    np[i].cap = 0;
  }
  g_snap_cache = np;
  g_snap_cache_count = grow;
}
static zan_blur_cache_t *zan_snap_slot(int slot) {
  if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS)
    return NULL;
  zan_snap_ensure(slot);
  if (slot >= g_snap_cache_count)
    return NULL;
  return &g_snap_cache[slot];
}

static void zan_cache_drop_surface(int sid) {
  for (int i = 0; i < ZAN_BLUR_CACHE_SLOTS; i++) {
    if (g_blur_cache[i].glass.pixels && g_blur_cache[i].glass.sid == sid) {
      zan_glass_drop(&g_blur_cache[i].glass);
    }
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
  if (!c)
    return;
  int x0 = clamp_i(x, 0, s->width);
  int y0 = clamp_i(y, 0, s->height);
  int x1 = clamp_i(x + w, 0, s->width);
  int y1 = clamp_i(y + h, 0, s->height);
  int rw = x1 - x0, rh = y1 - y0;
  if (rw <= 0 || rh <= 0)
    return;
  region_read(s, x0, y0, x1, y1);
  ZAN_STAT(g_st_snap, (long long)rw * (long long)rh);
  size_t n = (size_t)rw * (size_t)rh;
  if (c->cap < n) {
#if defined(__ANDROID__)
    size_t keep = c->pixels ? c->cap * sizeof(u32) : 0;
    if (ZAN_SNAP_CACHE_BYTES &&
        zan_snap_total_bytes() - keep + n * sizeof(u32) >
            (size_t)ZAN_SNAP_CACHE_BYTES) {
      c->valid = 0;
      return;
    }
#endif
    u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
    if (!np) {
      c->valid = 0;
      return;
    }
    c->pixels = np;
    c->cap = n;
  }
  size_t row_bytes = (size_t)rw * sizeof(u32);
  for (int j = 0; j < rh; j++) {
    memcpy(c->pixels + (size_t)j * (size_t)rw,
           s->pixels + (size_t)(y0 + j) * (size_t)s->stride + x0,
           row_bytes);
  }
  c->x0 = x0;
  c->y0 = y0;
  c->rw = rw;
  c->rh = rh;
  c->r = 0;
  c->sid = s->id;
  c->valid = 1;
}

EXPORT void zan_gui_snapshot_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                  i32 slot) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, snapshot)->snapshot(s, (int)x, (int)y, (int)w, (int)h, (int)slot);
}

EXPORT void zan_gui_snapshot_patch_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                        i32 h, i32 slot) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return;
  ZAN_IMPL(s, snapshot_patch)
      ->snapshot_patch(s, (int)x, (int)y, (int)w, (int)h, (int)slot);
}

static void cpu_snapshot_patch(zan_surface_t *s, int x, int y, int w, int h,
                               int slot) {
  zan_blur_cache_t *c = zan_snap_slot(slot);
  if (!c)
    return;
  if (!c->valid || !c->pixels || c->sid != s->id)
    return;
  int x0 = clamp_i(x, 0, s->width);
  int y0 = clamp_i(y, 0, s->height);
  int x1 = clamp_i(x + w, 0, s->width);
  int y1 = clamp_i(y + h, 0, s->height);
  if (x1 <= x0 || y1 <= y0)
    return;
  if (x0 < c->x0 || y0 < c->y0 || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
    return;
  }
  region_read(s, x0, y0, x1, y1);
  size_t row_bytes = (size_t)(x1 - x0) * sizeof(u32);
  for (int j = y0; j < y1; j++) {
    memcpy(c->pixels + (size_t)(j - c->y0) * (size_t)c->rw + (x0 - c->x0),
           s->pixels + (size_t)j * (size_t)s->stride + x0,
           row_bytes);
  }
}

static int cpu_restore_rect(zan_surface_t *s, int x, int y, int w, int h,
                            int slot) {
  zan_blur_cache_t *c = zan_snap_slot(slot);
  if (!c)
    return 0;
  int x0 = clamp_i(x, 0, s->width);
  int y0 = clamp_i(y, 0, s->height);
  int x1 = clamp_i(x + w, 0, s->width);
  int y1 = clamp_i(y + h, 0, s->height);
  int rw = x1 - x0, rh = y1 - y0;
  if (rw <= 0 || rh <= 0)
    return 0;
  if (!c->valid || !c->pixels || c->sid != s->id || c->x0 != x0 ||
      c->y0 != y0 || c->rw != rw || c->rh != rh) {
    return 0;
  }
  int cx0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
  int cy0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
  int cx1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
  int cy1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
  if (cx1 <= cx0 || cy1 <= cy0)
    return 1;
  ZAN_STAT(g_st_restore, (long long)(cx1 - cx0) * (long long)(cy1 - cy0));
  size_t row_bytes = (size_t)(cx1 - cx0) * sizeof(u32);
  for (int j = cy0; j < cy1; j++) {
    memcpy(s->pixels + (size_t)j * (size_t)s->stride + cx0,
           c->pixels + (size_t)(j - y0) * (size_t)rw + (cx0 - x0),
           row_bytes);
  }
  return 1;
}

/* 局部视口快照区域还原恢复 */
static int cpu_restore_sub_rect(zan_surface_t *s, int x, int y, int w, int h,
                                int slot) {
  zan_blur_cache_t *c = zan_snap_slot(slot);
  if (!c)
    return 0;
  if (!c->valid || !c->pixels || c->sid != s->id)
    return 0;
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (x1 <= x0 || y1 <= y0)
    return 1;
  if (x0 < c->x0 || y0 < c->y0 || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
    return 0;
  }
  if (x0 < s->clip_x0)
    x0 = s->clip_x0;
  if (y0 < s->clip_y0)
    y0 = s->clip_y0;
  if (x1 > s->clip_x1)
    x1 = s->clip_x1;
  if (y1 > s->clip_y1)
    y1 = s->clip_y1;
  x0 = clamp_i(x0, 0, s->width);
  y0 = clamp_i(y0, 0, s->height);
  x1 = clamp_i(x1, 0, s->width);
  y1 = clamp_i(y1, 0, s->height);
  if (x1 <= x0 || y1 <= y0)
    return 1;
  ZAN_STAT(g_st_restore, (long long)(x1 - x0) * (long long)(y1 - y0));
  size_t row_bytes = (size_t)(x1 - x0) * sizeof(u32);
  for (int j = y0; j < y1; j++) {
    memcpy(s->pixels + (size_t)j * (size_t)s->stride + x0,
           c->pixels + (size_t)(j - c->y0) * (size_t)c->rw + (x0 - c->x0),
           row_bytes);
  }
  return 1;
}

static int cpu_restore(zan_surface_t *s, int x, int y, int w, int h, int slot,
                       int sub_rect) {
  return sub_rect ? cpu_restore_sub_rect(s, x, y, w, h, slot)
                  : cpu_restore_rect(s, x, y, w, h, slot);
}

EXPORT i32 zan_gui_restore_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                i32 slot) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return 0;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return 0;
  region_write(s, x, y, (int64_t)x + w, (int64_t)y + h);
  return ZAN_IMPL(s, restore)
      ->restore(s, (int)x, (int)y, (int)w, (int)h, (int)slot, 0);
}

EXPORT i32 zan_gui_restore_sub_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h,
                                    i32 slot) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return 0;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s)
    return 0;
  region_write(s, x, y, (int64_t)x + w, (int64_t)y + h);
  return ZAN_IMPL(s, restore)
      ->restore(s, (int)x, (int)y, (int)w, (int)h, (int)slot, 1);
}

EXPORT i32 zan_gui_surface_dump(i32 surface_id, const char *path) {
  if (surface_id < 0 || surface_id >= g_surface_count)
    return 0;
  zan_surface_t *s = g_surfaces[surface_id];
  if (s)
    region_read(s, 0, 0, s->width, s->height);
  if (!s || !s->pixels || !path)
    return 0;
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  int w = s->width, h = s->height;
  int stride_out = (w * 3 + 3) & ~3;
  u32 data_size = (u32)stride_out * (u32)h;
  u32 file_size = 54 + data_size;
  unsigned char hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B';
  hdr[1] = 'M';
  memcpy(hdr + 2, &file_size, 4);
  u32 off = 54;
  memcpy(hdr + 10, &off, 4);
  u32 ihs = 40;
  memcpy(hdr + 14, &ihs, 4);
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4);
  unsigned short planes = 1;
  memcpy(hdr + 26, &planes, 2);
  unsigned short bpp = 24;
  memcpy(hdr + 28, &bpp, 2);
  memcpy(hdr + 34, &data_size, 4);
  fwrite(hdr, 1, 54, f);
  unsigned char *row = (unsigned char *)malloc((size_t)stride_out);
  if (!row) {
    fclose(f);
    return 0;
  }
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
  if (!c)
    return;
  free(c->pixels);
  c->pixels = NULL;
  c->cap = 0;
  c->valid = 0;
}
