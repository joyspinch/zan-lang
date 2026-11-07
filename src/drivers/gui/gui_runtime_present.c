/* gui_runtime_present.c -- Win32 GDI / 分块脏矩形 / 分层窗口原子呈现子模块 */

#ifdef _WIN32

typedef struct zan_present_shadow_s {
    void *hwnd;
    int w;
    int h;
    u32 *pixels;
    struct zan_present_shadow_s *next;
} zan_present_shadow_t;

static zan_present_shadow_t *g_present_shadows = NULL;

#define ZAN_DIFF_TILE 64

static zan_present_shadow_t *present_shadow(void *hwnd, int w, int h, int alloc) {
    zan_present_shadow_t *prev = NULL;
    zan_present_shadow_t *it = g_present_shadows;
    int n = 0;
    while (it) {
        if (it->hwnd == hwnd) {
            if (it->w != w || it->h != h) {
                if (!alloc) return NULL;
                u32 *np = (u32 *)realloc(it->pixels,
                                         (size_t)w * (size_t)h * 4);
                if (!np) return NULL;
                memset(np, 0, (size_t)w * (size_t)h * 4);
                it->pixels = np;
                it->w = w;
                it->h = h;
            }
            if (prev) {  /* move to front (LRU) */
                prev->next = it->next;
                it->next = g_present_shadows;
                g_present_shadows = it;
            }
            return it;
        }
        prev = it;
        it = it->next;
        n++;
    }
    if (!alloc) return NULL;
    zan_present_shadow_t *ns = (zan_present_shadow_t *)calloc(1, sizeof(*ns));
    if (!ns) return NULL;
    ns->pixels = (u32 *)calloc((size_t)w * (size_t)h, 4);
    if (!ns->pixels) { free(ns); return NULL; }
    ns->hwnd = hwnd;
    ns->w = w;
    ns->h = h;
    ns->next = g_present_shadows;
    g_present_shadows = ns;
    if (n >= 8) {
        zan_present_shadow_t *tail = g_present_shadows;
        while (tail->next && tail->next->next) tail = tail->next;
        free(tail->next->pixels);
        free(tail->next);
        tail->next = NULL;
    }
    return ns;
}

static void present_shadow_fill_rect(zan_present_shadow_t *sh, const u32 *pix,
                                     int stride, int rx, int ry, int rw, int rh) {
    for (int j = 0; j < rh; j++) {
        memcpy(sh->pixels + (size_t)(ry + j) * (size_t)sh->w + rx,
               pix + (size_t)(ry + j) * (size_t)stride + rx,
               (size_t)rw * 4);
    }
}

#define ZAN_CHUNK_H 128

static int gdi_upload_chunks(HDC dc, u32 *pix, BITMAPINFO *bmi,
                             int rx, int ry, int rw, int rh) {
    int calls = 0;
    int y = ry;
    int end = ry + rh;
    int w = bmi->bmiHeader.biWidth;
    HDC sdc = GetDC(0);
    HDC mdc = CreateCompatibleDC(sdc);
    ReleaseDC(0, sdc);
    if (!mdc) return calls;
    BITMAPINFOHEADER cbh = bmi->bmiHeader;
    cbh.biWidth = rw;
    cbh.biHeight = -(ZAN_CHUNK_H > rh ? ZAN_CHUNK_H : rh);
    void *stage = NULL;
    HBITMAP bm = CreateDIBSection(mdc, (BITMAPINFO *)&cbh, DIB_RGB_COLORS,
                                  &stage, NULL, 0);
    if (!bm) {
        DeleteDC(mdc);
        return calls;
    }
    HGDIOBJ old = SelectObject(mdc, bm);
    while (y < end) {
        int ch = end - y;
        if (ch > ZAN_CHUNK_H) ch = ZAN_CHUNK_H;
        for (int j = 0; j < ch; j++) {
            memcpy((u32 *)stage + (size_t)j * rw,
                   pix + (size_t)(y + j) * (size_t)w + rx,
                   (size_t)rw * 4);
        }
        BitBlt(dc, rx, y, rw, ch, mdc, 0, 0, SRCCOPY);
        calls++;
        y += ch;
    }
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    return calls;
}

#endif /* _WIN32 */

EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id);

#define ZAN_ATOMIC_AT 35
#define ZAN_ATOMIC_RET 1000000
EXPORT i32 zan_gui_gdi_present(void *hwnd, i32 surface_id, i32 *rects,
                               i32 rect_count) {
#ifdef _WIN32
    if (!hwnd || surface_id < 0 || surface_id >= g_surface_count) return -1;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels) return -1;
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    int w = s->width, h = s->height;
    u32 *pix = s->pixels;
    int stride = s->stride;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC((HWND)hwnd);
    if (!dc) return -1;
    int calls = 0;

    zan_present_shadow_t *sh = present_shadow(hwnd, w, h, 1);
    int force_full = (rect_count < 0) || !sh;
    {
        static int tilediff = -1;
        if (tilediff < 0) {
            const char *ev = getenv("ZAN_TILE_DIFF");
            tilediff = ev && *ev;
        }
        if (!tilediff && rect_count == 0) force_full = 1;
    }

    if (!force_full && rect_count == 0) {
        int tw = (w + ZAN_DIFF_TILE - 1) / ZAN_DIFF_TILE;
        int th = (h + ZAN_DIFF_TILE - 1) / ZAN_DIFF_TILE;
        unsigned char *changed = (unsigned char *)calloc((size_t)tw * th, 1);
        unsigned char *rowany = (unsigned char *)calloc((size_t)th, 1);
        int *out = (int *)malloc(sizeof(int) * 4 * (size_t)(tw * th + th));
        if (!changed || !out || !rowany) {
            free(changed);
            free(out);
            free(rowany);
            ReleaseDC((HWND)hwnd, dc);
            return -1;
        }
        int changed_tiles = 0;
        for (int ty = 0; ty < th; ty++) {
            int y0 = ty * ZAN_DIFF_TILE;
            int rows = (y0 + ZAN_DIFF_TILE <= h) ? ZAN_DIFF_TILE : (h - y0);
            for (int tx = 0; tx < tw; tx++) {
                int x0 = tx * ZAN_DIFF_TILE;
                int cols = (x0 + ZAN_DIFF_TILE <= w) ? ZAN_DIFF_TILE : (w - x0);
                int dirty = 0;
                for (int j = 0; j < rows && !dirty; j++) {
                    u32 *a = pix + (size_t)(y0 + j) * (size_t)stride + x0;
                    u32 *b = sh->pixels + (size_t)(y0 + j) * (size_t)w + x0;
                    if (memcmp(a, b, (size_t)cols * 4) != 0) dirty = 1;
                }
                if (dirty) {
                    changed[(size_t)ty * tw + tx] = 1;
                    changed_tiles++;
                }
            }
        }
        if (changed_tiles > 0) {
            long long area = (long long)changed_tiles * ZAN_DIFF_TILE
                             * ZAN_DIFF_TILE;
            if (area * 100 > (long long)w * h * ZAN_ATOMIC_AT
                    && zan_gui_atomic_present(hwnd, surface_id)) {
                memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
                free(changed);
                free(out);
                free(rowany);
                ReleaseDC((HWND)hwnd, dc);
                return ZAN_ATOMIC_RET;
            }
            for (int ty = 0; ty < th; ty++) {
                for (int tx = 0; tx < tw; tx++) {
                    if (changed[(size_t)ty * tw + tx]) { rowany[ty] = 1; break; }
                }
            }
            int n = 0;
            for (int ty = 0; ty < th; ty++) {
                int tx = 0;
                while (tx < tw) {
                    if (!changed[(size_t)ty * tw + tx]) { tx++; continue; }
                    int tx0 = tx;
                    while (tx < tw && changed[(size_t)ty * tw + tx]) tx++;
                    int ty1 = ty + 1;
                    while (ty1 < th) {
                        int ok = 1;
                        for (int k = tx0; k < tx; k++) {
                            if (!changed[(size_t)ty1 * tw + k]) { ok = 0; break; }
                        }
                        if (!ok) break;
                        ty1++;
                    }
                    out[n * 4 + 0] = tx0 * ZAN_DIFF_TILE;
                    out[n * 4 + 1] = ty * ZAN_DIFF_TILE;
                    out[n * 4 + 2] = (tx - tx0) * ZAN_DIFF_TILE;
                    out[n * 4 + 3] = (ty1 - ty) * ZAN_DIFF_TILE;
                    n++;
                    for (int yy = ty; yy < ty1; yy++) {
                        for (int k = tx0; k < tx; k++) {
                            changed[(size_t)yy * tw + k] = 0;
                        }
                    }
                }
            }
            if (n > 48) {
                int ty = 0;
                n = 0;
                while (ty < th) {
                    if (!rowany[ty]) { ty++; continue; }
                    int ty0 = ty;
                    while (ty < th && rowany[ty]) ty++;
                    out[n * 4 + 0] = 0;
                    out[n * 4 + 1] = ty0 * ZAN_DIFF_TILE;
                    out[n * 4 + 2] = w;
                    int bh = (ty - ty0) * ZAN_DIFF_TILE;
                    if (out[n * 4 + 1] + bh > h) bh = h - out[n * 4 + 1];
                    out[n * 4 + 3] = bh;
                    n++;
                }
            }
            for (int i = 0; i < n; i++) {
                int rx = out[i * 4 + 0];
                int ry = out[i * 4 + 1];
                int rw = out[i * 4 + 2];
                int rh = out[i * 4 + 3];
                if (rx < 0) { rw += rx; rx = 0; }
                if (ry < 0) { rh += ry; ry = 0; }
                if (rx + rw > w) rw = w - rx;
                if (ry + rh > h) rh = h - ry;
                if (rw <= 0 || rh <= 0) continue;
                calls += gdi_upload_chunks(dc, pix, &bmi, rx, ry, rw, rh);
                present_shadow_fill_rect(sh, pix, stride, rx, ry, rw, rh);
            }
        }
        free(changed);
        free(out);
        free(rowany);
    } else if (force_full) {
        if (zan_gui_atomic_present(hwnd, surface_id)) {
            memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
            ReleaseDC((HWND)hwnd, dc);
            return ZAN_ATOMIC_RET;
        }
        calls = gdi_upload_chunks(dc, pix, &bmi, 0, 0, w, h);
        memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
    } else if (rect_count > 0) {
        long long area = 0;
        for (int i = 0; i < rect_count; i++) {
            int rx = rects[i * 4 + 0];
            int ry = rects[i * 4 + 1];
            int rw = rects[i * 4 + 2];
            int rh = rects[i * 4 + 3];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > w) rw = w - rx;
            if (ry + rh > h) rh = h - ry;
            if (rw <= 0 || rh <= 0) continue;
            area += (long long)rw * rh;
        }
        if (area * 100 > (long long)w * h * ZAN_ATOMIC_AT
                && zan_gui_atomic_present(hwnd, surface_id)) {
            memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
            ReleaseDC((HWND)hwnd, dc);
            return ZAN_ATOMIC_RET;
        }
        for (int i = 0; i < rect_count; i++) {
            int rx = rects[i * 4 + 0];
            int ry = rects[i * 4 + 1];
            int rw = rects[i * 4 + 2];
            int rh = rects[i * 4 + 3];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > w) rw = w - rx;
            if (ry + rh > h) rh = h - ry;
            if (rw <= 0 || rh <= 0) continue;
            calls += gdi_upload_chunks(dc, pix, &bmi, rx, ry, rw, rh);
            present_shadow_fill_rect(sh, pix, stride, rx, ry, rw, rh);
        }
    }
    ReleaseDC((HWND)hwnd, dc);
    return calls;
#else
    (void)hwnd; (void)surface_id; (void)rects; (void)rect_count;
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
                it->bm = CreateDIBSection(it->dc, &bmi, DIB_RGB_COLORS,
                                          &it->bits, NULL, 0);
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
    if (!nl) return NULL;
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
    if (!nl->dc) { free(nl); return NULL; }
    nl->bm = CreateDIBSection(nl->dc, &bmi, DIB_RGB_COLORS, &nl->bits, NULL, 0);
    if (!nl->bm) { DeleteDC(nl->dc); nl->dc = 0; free(nl); return NULL; }
    SelectObject(nl->dc, nl->bm);
    nl->hwnd = hwnd;
    nl->w = w;
    nl->h = h;
    nl->next = g_atomic_layers;
    g_atomic_layers = nl;
    if (n >= 8) {
        zan_atomic_layer_t *tail = g_atomic_layers;
        while (tail->next && tail->next->next) tail = tail->next;
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
    if (!hwnd || surface_id < 0 || surface_id >= g_surface_count) return 0;
    if (!(GetWindowLongPtrW((HWND)hwnd, GWL_EXSTYLE) & WS_EX_LAYERED)) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels) return 0;
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    RECT rc;
    if (!GetClientRect((HWND)hwnd, &rc)) return 0;
    int pw = rc.right - rc.left;
    int ph = rc.bottom - rc.top;
    if (pw <= 0 || ph <= 0) return 0;
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
        if (tw > 0 && th > 0 && tx >= 0 && ty >= 0 && tx + pw <= tw
                && ty + ph <= th) {
            ww = tw;
            wh = th;
            ox = tx;
            oy = ty;
        }
    }
    zan_atomic_layer_t *l = atomic_layer(hwnd, ww, wh);
    if (!l || !l->bits) return 0;
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
            for (; x < ox; x++) { drow[x] = 0; }
            for (; x < ox + copyW; x++) {
                drow[x] = srow[x - ox] | 0xFF000000u;
            }
            for (; x < ww; x++) { drow[x] = 0; }
        }
    }
    POINT src = { 0, 0 };
    SIZE size = { ww, wh };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC sdc = GetDC(0);
    BOOL ok = UpdateLayeredWindow((HWND)hwnd, sdc, NULL, &size, l->dc, &src,
                                  0, &bf, 2);
    ReleaseDC(0, sdc);
    return ok ? 1 : 0;
#else
    (void)hwnd; (void)surface_id;
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
            free(dead->pixels);
            free(dead);
            return;
        }
        p = &(*p)->next;
    }
    zan_atomic_layer_drop(hwnd);
#else
    (void)hwnd;
#endif
}
