/* gui_runtime_present.c -- Win32 GDI 差分上传与分层窗口原子呈现 */

#ifdef _WIN32

typedef struct zan_present_shadow_s {
  void *hwnd;
  int w, h;
  int surface_id;
  int valid;
  HDC dc;
  HBITMAP bitmap;
  u32 *pixels;
  struct zan_present_shadow_s *next;
} zan_present_shadow_t;

static zan_present_shadow_t *g_present_shadows = NULL;

static BITMAPINFO present_bitmap_info(int w, int h) {
  BITMAPINFO info = {0};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  return info;
}

static void present_shadow_free(zan_present_shadow_t *sh) {
  DeleteDC(sh->dc);
  DeleteObject(sh->bitmap);
  free(sh);
}

static zan_present_shadow_t *present_shadow(void *hwnd, HDC dc, int w, int h) {
  zan_present_shadow_t **link = &g_present_shadows;
  int count = 0;
  while (*link && (*link)->hwnd != hwnd) {
    link = &(*link)->next;
    count++;
  }
  zan_present_shadow_t *sh = *link;
  if (sh) {
    *link = sh->next;
    if (sh->w == w && sh->h == h) {
      sh->next = g_present_shadows;
      g_present_shadows = sh;
      return sh;
    }
    present_shadow_free(sh);
  }
  for (; *link; link = &(*link)->next)
    count++;

  sh = (zan_present_shadow_t *)calloc(1, sizeof(*sh));
  if (!sh)
    return NULL;
  BITMAPINFO info = present_bitmap_info(w, h);
  void *bits = NULL;
  sh->dc = CreateCompatibleDC(dc);
  if (sh->dc) {
    sh->bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
  }
  if (!sh->bitmap || !bits || !SelectObject(sh->dc, sh->bitmap)) {
    present_shadow_free(sh);
    return NULL;
  }
  sh->hwnd = hwnd;
  sh->w = w;
  sh->h = h;
  sh->pixels = (u32 *)bits;
  sh->next = g_present_shadows;
  g_present_shadows = sh;
  if (count >= 8) {
    link = &g_present_shadows;
    while ((*link)->next)
      link = &(*link)->next;
    present_shadow_free(*link);
    *link = NULL;
  }
  return sh;
}

static void present_shadow_invalidate(void *hwnd) {
  for (zan_present_shadow_t *sh = g_present_shadows; sh; sh = sh->next) {
    if (sh->hwnd == hwnd) {
      sh->valid = 0;
      return;
    }
  }
}

static void present_shadow_forget_surface(int surface_id) {
  for (zan_present_shadow_t *sh = g_present_shadows; sh; sh = sh->next) {
    if (sh->surface_id == surface_id)
      sh->valid = 0;
  }
}

static int present_bounds(const zan_surface_t *s, const u32 *previous,
                          RECT *r) {
  int left = s->width, top = s->height, right = 0, bottom = 0;
  size_t bytes = (size_t)s->width * sizeof(u32);
  for (int y = 0; y < s->height; y++) {
    const u32 *row = s->pixels + (size_t)y * s->stride;
    const u32 *old = previous + (size_t)y * s->width;
    if (memcmp(row, old, bytes) == 0)
      continue;
    if (y < top)
      top = y;
    bottom = y + 1;
    for (int x = 0; x < left; x++) {
      if (row[x] != old[x]) {
        left = x;
        break;
      }
    }
    for (int x = s->width; x > right; x--) {
      if (row[x - 1] != old[x - 1]) {
        right = x;
        break;
      }
    }
  }
  *r = (RECT){left, top, right, bottom};
  return top < bottom;
}

static RECT present_clip_rect(const i32 *rect, int w, int h) {
  if (rect[2] <= 0 || rect[3] <= 0)
    return (RECT){0};
  int64_t right = (int64_t)rect[0] + rect[2];
  int64_t bottom = (int64_t)rect[1] + rect[3];
  return (RECT){rect[0] > 0 ? rect[0] : 0, rect[1] > 0 ? rect[1] : 0,
                right < w ? (LONG)right : w, bottom < h ? (LONG)bottom : h};
}

static int gdi_upload(HDC dc, zan_present_shadow_t *sh, const zan_surface_t *s,
                      RECT r) {
  int width = r.right - r.left;
  int height = r.bottom - r.top;
  if (sh) {
    for (int y = r.top; y < r.bottom; y++) {
      memcpy(sh->pixels + (size_t)y * sh->w + r.left,
             s->pixels + (size_t)y * s->stride + r.left,
             (size_t)width * sizeof(u32));
    }
    BOOL copied = BitBlt(dc, r.left, r.top, width, height, sh->dc, r.left,
                         r.top, SRCCOPY);
    /* DIB 像素由 CPU 和 GDI 共用。等待读取结束后才能再次写入。 */
    BOOL flushed = GdiFlush();
    return copied && flushed ? 1 : -1;
  }
  BITMAPINFO info = present_bitmap_info(s->stride, s->height);
  int lines =
      StretchDIBits(dc, r.left, r.top, width, height, r.left, r.top, width,
                    height, s->pixels, &info, DIB_RGB_COLORS, SRCCOPY);
  return lines > 0 ? 1 : -1;
}

#endif /* _WIN32 */

EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id);

#define ZAN_ATOMIC_RET 1000000

EXPORT i32 zan_gui_gdi_present(void *hwnd, i32 surface_id, i32 *rects,
                               i32 rect_count) {
#ifdef _WIN32
  if (!hwnd || surface_id < 0 || surface_id >= g_surface_count)
    return -1;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !s->pixels || (rect_count > 0 && !rects))
    return -1;
  if (s->be) {
    if (s->be->flush)
      s->be->flush(s);
    if (s->be->read_pixels)
      s->be->read_pixels(s);
  }
  int w = s->width, h = s->height;
  RECT full = {0, 0, w, h};
  int layered =
      (GetWindowLongPtrW((HWND)hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
  static int diff_enabled = -1;
  if (diff_enabled < 0) {
    const char *setting = getenv("ZAN_TILE_DIFF");
    diff_enabled = setting && *setting;
  }
  int force_full = rect_count < 0 || (rect_count == 0 && !diff_enabled);
  if (layered) {
    present_shadow_invalidate(hwnd);
    force_full = rect_count <= 0;
    int64_t area = 0;
    for (int i = 0; !force_full && i < rect_count; i++) {
      RECT r = present_clip_rect(rects + (size_t)i * 4, w, h);
      if (r.left < r.right && r.top < r.bottom) {
        area += (int64_t)(r.right - r.left) * (r.bottom - r.top);
        force_full = area * 100 > (int64_t)w * h * 35;
      }
    }
    if (force_full && zan_gui_atomic_present(hwnd, surface_id))
      return ZAN_ATOMIC_RET;
  }

  HDC dc = GetDC((HWND)hwnd);
  if (!dc) {
    present_shadow_invalidate(hwnd);
    return -1;
  }
  zan_present_shadow_t *sh = present_shadow(hwnd, dc, w, h);
  int visible = 0;
  if (!layered) {
    RECT clip;
    visible = GetClipBox(dc, &clip) == SIMPLEREGION && clip.left <= 0 &&
              clip.top <= 0 && clip.right >= w && clip.bottom >= h;
    force_full = force_full || !visible || !sh || !sh->valid ||
                 sh->surface_id != surface_id;
  } else if (!sh) {
    force_full = 1;
  }

  int calls = 0;
  if (force_full) {
    calls = gdi_upload(dc, sh, s, full);
  } else if (rect_count == 0) {
    RECT r;
    if (present_bounds(s, sh->pixels, &r))
      calls = gdi_upload(dc, sh, s, r);
  } else {
    for (int i = 0; i < rect_count; i++) {
      RECT r = present_clip_rect(rects + (size_t)i * 4, w, h);
      if (r.left >= r.right || r.top >= r.bottom)
        continue;
      if (gdi_upload(dc, sh, s, r) < 0) {
        calls = -1;
        break;
      }
      calls++;
    }
  }
  if (sh) {
    sh->surface_id = surface_id;
    sh->valid = visible && calls >= 0;
    if (calls < 0) {
      // 位图上传失败时直接补整窗，下一次重新建立差分基线。
      calls = gdi_upload(dc, NULL, s, full);
    }
  }
  ReleaseDC((HWND)hwnd, dc);
  return calls;
#else
  (void)hwnd;
  (void)surface_id;
  (void)rects;
  (void)rect_count;
  return -1;
#endif
}

#ifdef _WIN32
typedef struct zan_atomic_layer_s {
  void *hwnd;
  int w, h;
  HBITMAP bm;
  HDC dc;
  void *bits;
  struct zan_atomic_layer_s *next;
} zan_atomic_layer_t;

static zan_atomic_layer_t *g_atomic_layers = NULL;

static zan_atomic_layer_t *atomic_layer(void *hwnd, int w, int h) {
  zan_atomic_layer_t *prev = NULL;
  zan_atomic_layer_t *it = g_atomic_layers;
  int n = 0;
  while (it) {
    if (it->hwnd == hwnd) {
      if (it->w != w || it->h != h) {
        DeleteDC(it->dc);
        DeleteObject(it->bm);
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        HDC sdc = GetDC(0);
        it->dc = CreateCompatibleDC(sdc);
        ReleaseDC(0, sdc);
        it->bm =
            CreateDIBSection(it->dc, &bmi, DIB_RGB_COLORS, &it->bits, NULL, 0);
        if (!it->bm) {
          DeleteDC(it->dc);
          it->dc = 0;
          it->bits = NULL;
          return NULL;
        }
        SelectObject(it->dc, it->bm);
        it->w = w;
        it->h = h;
      }
      if (prev) {
        prev->next = it->next;
        it->next = g_atomic_layers;
        g_atomic_layers = it;
      }
      return it;
    }
    prev = it;
    it = it->next;
    n++;
  }
  zan_atomic_layer_t *nl = (zan_atomic_layer_t *)calloc(1, sizeof(*nl));
  if (!nl)
    return NULL;
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  HDC sdc = GetDC(0);
  nl->dc = CreateCompatibleDC(sdc);
  ReleaseDC(0, sdc);
  if (!nl->dc) {
    free(nl);
    return NULL;
  }
  nl->bm = CreateDIBSection(nl->dc, &bmi, DIB_RGB_COLORS, &nl->bits, NULL, 0);
  if (!nl->bm) {
    DeleteDC(nl->dc);
    nl->dc = 0;
    free(nl);
    return NULL;
  }
  SelectObject(nl->dc, nl->bm);
  nl->hwnd = hwnd;
  nl->w = w;
  nl->h = h;
  nl->next = g_atomic_layers;
  g_atomic_layers = nl;
  if (n >= 8) {
    zan_atomic_layer_t *tail = g_atomic_layers;
    while (tail->next && tail->next->next)
      tail = tail->next;
    DeleteDC(tail->next->dc);
    DeleteObject(tail->next->bm);
    free(tail->next);
    tail->next = NULL;
  }
  return nl;
}

static void zan_atomic_layer_drop(void *hwnd) {
  zan_atomic_layer_t **p = &g_atomic_layers;
  while (*p) {
    if ((*p)->hwnd == hwnd) {
      zan_atomic_layer_t *dead = *p;
      *p = dead->next;
      DeleteDC(dead->dc);
      DeleteObject(dead->bm);
      free(dead);
      return;
    }
    p = &(*p)->next;
  }
}
#endif

/* 帧缓冲区向物理屏幕原子呈现提交 */
EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id) {
#ifdef _WIN32
  if (!hwnd || surface_id < 0 || surface_id >= g_surface_count)
    return 0;
  if (!(GetWindowLongPtrW((HWND)hwnd, GWL_EXSTYLE) & WS_EX_LAYERED))
    return 0;
  zan_surface_t *s = g_surfaces[surface_id];
  if (!s || !s->pixels)
    return 0;
  if (s->be) {
    if (s->be->flush)
      s->be->flush(s);
    if (s->be->read_pixels)
      s->be->read_pixels(s);
  }
  RECT rc;
  if (!GetClientRect((HWND)hwnd, &rc))
    return 0;
  int pw = rc.right - rc.left;
  int ph = rc.bottom - rc.top;
  if (pw <= 0 || ph <= 0)
    return 0;
  RECT wr;
  POINT co;
  co.x = 0;
  co.y = 0;
  int ww = pw;
  int wh = ph;
  int ox = 0;
  int oy = 0;
  if (GetWindowRect((HWND)hwnd, &wr) && ClientToScreen((HWND)hwnd, &co)) {
    int tw = wr.right - wr.left;
    int th = wr.bottom - wr.top;
    int tx = co.x - wr.left;
    int ty = co.y - wr.top;
    if (tw > 0 && th > 0 && tx >= 0 && ty >= 0 && tx + pw <= tw &&
        ty + ph <= th) {
      ww = tw;
      wh = th;
      ox = tx;
      oy = ty;
    }
  }
  zan_atomic_layer_t *l = atomic_layer(hwnd, ww, wh);
  if (!l || !l->bits)
    return 0;
  int w = s->width, h = s->height;
  int stride = s->stride;
  u32 *pix = s->pixels;
  int copyW = w < ww ? w : ww;
  int copyH = h < wh ? h : wh;
  u32 *dst = (u32 *)l->bits;
  for (int y = 0; y < wh; y++) {
    u32 *drow = dst + (size_t)y * (size_t)ww;
    int sy = y - oy;
    if (sy < 0 || sy >= copyH) {
      memset(drow, 0, (size_t)ww * 4);
    } else {
      const u32 *srow = pix + (size_t)sy * (size_t)stride;
      int x = 0;
      for (; x < ox; x++) {
        drow[x] = 0;
      }
      for (; x < ox + copyW; x++) {
        drow[x] = srow[x - ox] | 0xFF000000u;
      }
      for (; x < ww; x++) {
        drow[x] = 0;
      }
    }
  }
  POINT src = {0, 0};
  SIZE size = {ww, wh};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  HDC sdc = GetDC(0);
  BOOL ok =
      UpdateLayeredWindow((HWND)hwnd, sdc, NULL, &size, l->dc, &src, 0, &bf, 2);
  ReleaseDC(0, sdc);
  return ok ? 1 : 0;
#else
  (void)hwnd;
  (void)surface_id;
  return 0;
#endif
}

EXPORT void zan_gui_gdi_present_drop(void *hwnd) {
#ifdef _WIN32
  zan_present_shadow_t **p = &g_present_shadows;
  while (*p) {
    if ((*p)->hwnd == hwnd) {
      zan_present_shadow_t *dead = *p;
      *p = dead->next;
      present_shadow_free(dead);
      break;
    }
    p = &(*p)->next;
  }
  zan_atomic_layer_drop(hwnd);
#else
  (void)hwnd;
#endif
}
