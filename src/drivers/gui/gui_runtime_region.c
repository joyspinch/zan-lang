/* A CPU surface owns one bounded background patch. Once the caller has drawn
 * its animation, any later write into the patch or paint-time read from it
 * invalidates replay. Test the actual raster bounds, including the live clip.
 */
enum { REGION_DRAWING = 1, REGION_WATCHING, REGION_SEALED };

static inline void region_access(zan_surface_t *s, int x0, int y0, int64_t x1,
                                 int64_t y1) {
  zan_paint_region *r = &s->region;
  if (r->state >= REGION_WATCHING && x0 < r->right && x1 > r->left &&
      y0 < r->bottom && y1 > r->top && x0 < x1 && y0 < y1)
    r->state = 0;
}

static inline void region_write(zan_surface_t *s, int x0, int y0, int64_t x1,
                                int64_t y1) {
  if (s->region.state < REGION_WATCHING)
    return;
  if (x0 < s->clip_x0)
    x0 = s->clip_x0;
  if (y0 < s->clip_y0)
    y0 = s->clip_y0;
  if (x1 > s->clip_x1)
    x1 = s->clip_x1;
  if (y1 > s->clip_y1)
    y1 = s->clip_y1;
  region_access(s, x0, y0, x1, y1);
}

static inline void region_read(zan_surface_t *s, int x0, int y0, int64_t x1,
                               int64_t y1) {
  if (s->region.state == REGION_WATCHING)
    region_access(s, x0, y0, x1, y1);
}

static inline void region_write_clip(zan_surface_t *s) {
  region_write(s, s->clip_x0, s->clip_y0, s->clip_x1, s->clip_y1);
}

static zan_surface_t *region_surface(i32 id) {
  return id >= 0 && id < g_surface_count ? g_surfaces[id] : NULL;
}

EXPORT i32 zan_gui_region_capture(i32 id, i32 x, i32 y, i32 w, i32 h, i32 *left,
                                  i32 *top, i32 *width, i32 *height) {
  zan_surface_t *s = region_surface(id);
  if (!s || (s->be && s->be != &zan_cpu_backend))
    return 0;
  zan_paint_region *r = &s->region;
  r->state = 0;
  int rw = s->clip_x1 - s->clip_x0, rh = s->clip_y1 - s->clip_y0;
  if (rw <= 0 || rh <= 0 || (int64_t)rw * rh > 65536 || s->clip_x0 < x ||
      s->clip_y0 < y || s->clip_x1 > (int64_t)x + w ||
      s->clip_y1 > (int64_t)y + h)
    return 0;
  size_t count = (size_t)rw * rh;
  if (count > r->capacity) {
    u32 *pixels = (u32 *)realloc(r->pixels, count * sizeof(u32));
    if (!pixels)
      return 0;
    r->pixels = pixels;
    r->capacity = count;
  }
  r->left = s->clip_x0;
  r->top = s->clip_y0;
  r->right = s->clip_x1;
  r->bottom = s->clip_y1;
  for (int row = 0; row < rh; row++) {
    memcpy(r->pixels + (size_t)row * rw,
           s->pixels + (size_t)(r->top + row) * s->stride + r->left,
           (size_t)rw * sizeof(u32));
  }
  *left = r->left;
  *top = r->top;
  *width = rw;
  *height = rh;
  r->state = REGION_DRAWING;
  return 1;
}

EXPORT void zan_gui_region_watch(i32 id) {
  zan_surface_t *s = region_surface(id);
  if (s && s->region.state == REGION_DRAWING)
    s->region.state = REGION_WATCHING;
}

EXPORT i32 zan_gui_region_seal(i32 id) {
  zan_surface_t *s = region_surface(id);
  if (!s || s->region.state != REGION_WATCHING || s->clip_depth != 0 ||
      s->clip_overflow != 0)
    return 0;
  s->region.state = REGION_SEALED;
  return 1;
}

EXPORT i32 zan_gui_region_restore(i32 id) {
  zan_surface_t *s = region_surface(id);
  if (!s || s->region.state != REGION_SEALED || s->clip_depth != 0 ||
      s->clip_overflow != 0)
    return 0;
  zan_paint_region *r = &s->region;
  int w = r->right - r->left;
  for (int y = r->top; y < r->bottom; y++) {
    memcpy(s->pixels + (size_t)y * s->stride + r->left,
           r->pixels + (size_t)(y - r->top) * w, (size_t)w * sizeof(u32));
  }
  r->state = REGION_DRAWING;
  return 1;
}

EXPORT void zan_gui_region_cancel(i32 id) {
  zan_surface_t *s = region_surface(id);
  if (s)
    s->region.state = 0;
}
