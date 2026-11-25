/* Included by gui_runtime_draw.c. Only the internal initial shape layer uses
 * these helpers. Straight alpha zero is independent of hidden destination RGB;
 * a zero source alpha still leaves every destination bit untouched. */
static void zan_initial_fill_rect(zan_surface_t *s, int x, int y, int w, int h,
                                  u32 c) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    u32 sa = c >> 24;
    if (x1 <= x0 || y1 <= y0 || !sa) return;
    long long assigned = 0, blended = 0;
    for (int py = y0; py < y1; py++) {
        u32 *row = s->pixels + py * s->stride;
        int px = x0;
        while (px < x1) {
            int start = px;
            int replace = sa == 255 || (row[px] >> 24) == 0;
            do { px++; } while (px < x1 &&
                (sa == 255 || (row[px] >> 24) == 0) == replace);
            if (replace) {
                fill_run_const(row + start, px - start, c);
                assigned += px - start;
            } else {
                blend_run_const(row + start, px - start, c);
                blended += px - start;
            }
        }
    }
    if (assigned) ZAN_STAT(g_st_fill_op, assigned);
    if (blended) ZAN_STAT(g_st_fill_blend, blended);
    /* Mixed rectangles have no honest single-area top entry. */
    if (!blended) stat_top_rect(1, (int)assigned, x0, y0, x1-x0, y1-y0, c);
    else if (!assigned) stat_top_rect(0, (int)blended, x0, y0, x1-x0, y1-y0, c);
}

static void zan_initial_fill_pixel(zan_surface_t *s, int x, int y, u32 c,
                                   int coverage) {
    if (clipped_out(s, x, y) || coverage <= 0) return;
    if (coverage > 255) coverage = 255;
    u32 a = (c >> 24) * (u32)coverage / 255;
    if (!a) return;
    c = (a << 24) | (c & 0xFFFFFFu);
    u32 *dst = s->pixels + y * s->stride + x;
    if (a == 255 || (*dst >> 24) == 0) {
        *dst = c;
        g_st_fill_op.px++;
    } else {
        *dst = blend_over(*dst, c);
        g_st_fill_blend.px++;
    }
}

static void zan_shape_fill_rect(zan_surface_t *s, int x, int y, int w, int h,
                                u32 c, int initial) {
    if (initial) zan_initial_fill_rect(s, x, y, w, h, c);
    else cpu_fill_rect(s, x, y, w, h, c);
}
