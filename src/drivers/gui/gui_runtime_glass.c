/* Included by gui_runtime_raster.c: CPU-only final static glass cache.
 * Full source rows are compared, never a sampled checksum. Partial clips and
 * unsupported backends leave drawing unchanged. Storage shares blur slots and
 * is capped globally; allocation failure is an ordinary uncached frame. */
#define ZAN_GLASS_CACHE_BYTES (64u * 1024u * 1024u)
static size_t g_glass_cache_bytes;
static uint64_t g_glass_cache_age;

static void zan_glass_drop(zan_glass_cache_t *c) {
  if (c->pixels) {
    g_glass_cache_bytes -= c->cap * sizeof(u32) * 2;
    free(c->pixels);
  }
  memset(c, 0, sizeof(*c));
}

static int zan_glass_reserve(zan_glass_cache_t *c, size_t n) {
  size_t bytes = n * sizeof(u32) * 2;
  if (bytes > ZAN_GLASS_CACHE_BYTES)
    return 0;
  if (c->cap == n && c->pixels)
    return 1;
  zan_glass_drop(c);
  while (g_glass_cache_bytes + bytes > ZAN_GLASS_CACHE_BYTES) {
    zan_glass_cache_t *oldest = NULL;
    for (int i = 0; i < ZAN_BLUR_CACHE_SLOTS; i++) {
      zan_glass_cache_t *v = &g_blur_cache[i].glass;
      if (v != c && v->pixels && !v->pending &&
          (!oldest || v->age < oldest->age))
        oldest = v;
    }
    if (!oldest)
      return 0;
    zan_glass_drop(oldest);
  }
  c->pixels = (u32 *)malloc(bytes);
  if (!c->pixels)
    return 0;
  c->cap = n;
  g_glass_cache_bytes += bytes;
  return 1;
}

/* Return 1 only after restoring final pixels; 0 always means paint normally.
 * The caller must pair every miss with end, before rendering child content.
 * Geometry is deliberately restricted to fully visible, fully clipped boxes. */
EXPORT i32 zan_gui_glass_begin(i32 surface_id, i32 slot, i32 x, i32 y, i32 w,
                               i32 h, i32 radius, i32 cr, i32 corners, i32 fill,
                               i32 sheen, i32 sheen_color, i32 sheen_cap,
                               i32 dirty) {
  if (surface_id < 0 || surface_id >= g_surface_count || slot < 0 ||
      slot >= ZAN_BLUR_CACHE_SLOTS)
    return 0;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || (s->be && s->be != &zan_cpu_backend) || !s->pixels)
    return 0;
  zan_glass_cache_t *c = &g_blur_cache[slot].glass;
  c->pending = 0;
  if (w <= 0 || h <= 0 || x < 0 || y < 0 || w > s->width || h > s->height ||
      x > s->width - w || y > s->height - h ||
      !clip_covers(s, x, y, x + w, y + h))
    return 0;
  region_read(s, x, y, x + w, y + h);
  region_write(s, x, y, x + w, y + h);
  int key[12] = {x,       y,    w,     h,           radius,    cr,
                 corners, fill, sheen, sheen_color, sheen_cap, dirty};
  size_t n = (size_t)w * (size_t)h;
  int matches = c->valid && c->sid == s->id && c->cap == n &&
                memcmp(c->key, key, sizeof(key)) == 0;
  for (int j = 0; matches && j < h; j++) {
    matches = memcmp(c->pixels + (size_t)j * w,
                     s->pixels + (size_t)(y + j) * s->stride + x,
                     (size_t)w * sizeof(u32)) == 0;
  }
  if (matches) {
    for (int j = 0; j < h; j++) {
      memcpy(s->pixels + (size_t)(y + j) * s->stride + x,
             c->pixels + n + (size_t)j * w, (size_t)w * sizeof(u32));
    }
    c->age = ++g_glass_cache_age;
    ZAN_STAT(g_st_restore, (long long)n);
    s->painted = 1;
    return 1;
  }
  c->valid = 0;
  /* A dirty exact-source miss cannot be delegated to sampled blur hashing. */
  if (dirty)
    g_blur_cache[slot].valid = 0;
  if (!zan_glass_reserve(c, n))
    return 0;
  for (int j = 0; j < h; j++) {
    memcpy(c->pixels + (size_t)j * w,
           s->pixels + (size_t)(y + j) * s->stride + x,
           (size_t)w * sizeof(u32));
  }
  ZAN_STAT(g_st_snap, (long long)n);
  memcpy(c->key, key, sizeof(key));
  c->sid = s->id;
  c->pending = 1;
  c->age = ++g_glass_cache_age;
  return 0;
}

EXPORT void zan_gui_glass_end(i32 surface_id, i32 slot) {
  if (surface_id < 0 || surface_id >= g_surface_count || slot < 0 ||
      slot >= ZAN_BLUR_CACHE_SLOTS)
    return;
  zan_surface_t *s = g_surfaces[surface_id];
  zan_glass_cache_t *c = &g_blur_cache[slot].glass;
  if (!c->pending || !s || (s->be && s->be != &zan_cpu_backend) ||
      c->sid != s->id)
    return;
  c->pending = 0;
  int x = c->key[0], y = c->key[1], w = c->key[2], h = c->key[3];
  if (x > s->width - w || y > s->height - h ||
      !clip_covers(s, x, y, x + w, y + h))
    return;
  region_read(s, x, y, x + w, y + h);
  size_t n = (size_t)w * (size_t)h;
  for (int j = 0; j < h; j++) {
    memcpy(c->pixels + n + (size_t)j * w,
           s->pixels + (size_t)(y + j) * s->stride + x,
           (size_t)w * sizeof(u32));
  }
  ZAN_STAT(g_st_snap, (long long)n);
  c->valid = 1;
}
