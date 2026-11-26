/* gui_runtime_dwrite.cpp -- DirectWrite fallback for glyphs the GDI face
 * lacks (emoji above all: Segoe UI Emoji is a color font GDI cannot paint).
 *
 * The Windows SDK's dwrite.h only parses as C++, so the COM side lives in
 * this file while the rest of the runtime stays C; gui_runtime_text.c calls
 * the three zan_dw_* entry points below. Loading is lazy and failure-
 * tolerant: no dwrite.dll (Win7 without the platform update) simply leaves
 * the fallback unavailable and text keeps GDI's behaviour.
 *
 * Colors: TranslateColorGlyphRun (factory2, Win 8.1+) yields the COLR
 * layers; palette index 0xFFFF layers are the mono outlines and draw in
 * the foreground color (white -- the tile is marked ZAN_TILE_RGBA so the
 * compositor applies no tint). The bitmap render target produces
 * premultiplied BGRA, converted to the straight alpha the atlas and both
 * compositors already expect (Linux FreeType CBDT shape).
 */
#include <windows.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static IDWriteFactory *g_factory = nullptr;
static IDWriteFactory2 *g_factory2 = nullptr;   /* Win 8.1+: color glyphs */
static IDWriteGdiInterop *g_gdi = nullptr;      /* bitmap render targets */
static IDWriteRenderingParams *g_params = nullptr;
static bool g_tried = false;

extern "C" int zan_dw_init(void) {
    if (g_tried) return g_factory ? 1 : 0;
    g_tried = true;
    HMODULE dll = LoadLibraryW(L"dwrite.dll");
    if (!dll) return 0;
    auto create = (HRESULT(WINAPI *)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **))
        GetProcAddress(dll, "DWriteCreateFactory");
    if (!create) return 0;
    if (FAILED(create(DWRITE_FACTORY_TYPE_SHARED,
                      __uuidof(IDWriteFactory), (IUnknown **)&g_factory))) {
        return 0;
    }
    /* Optional: without factory2 there is no color path and no system
     * fallback mapping -- callers keep GDI's behaviour. */
    g_factory->QueryInterface(__uuidof(IDWriteFactory2),
                              (void **)&g_factory2);
    g_factory->GetGdiInterop(&g_gdi);
    /* Flat geometry + natural mode: grayscale AA. ClearType stripes would
     * pollute color tiles, which compositors blend untinted. */
    g_factory->CreateCustomRenderingParams(1.0f, 0.0f, 0.0f,
        DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL,
        &g_params);
    return 1;
}

class ZanDwRenderer : public IDWriteTextRenderer {
public:
    ZanDwRenderer(IDWriteBitmapRenderTarget *brt,
                  IDWriteRenderingParams *params)
        : brt_(brt), params_(params), ref_(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void **out) noexcept override {
        *out = nullptr;
        if (riid == __uuidof(IDWriteTextRenderer)
            || riid == __uuidof(IUnknown)) {
            *out = static_cast<IDWriteTextRenderer *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) noexcept override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release(void) noexcept override { return --ref_; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(
        void *, BOOL *disabled) noexcept override {
        *disabled = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(
        void *, DWRITE_MATRIX *m) noexcept override {
        m->m11 = 1.0f; m->m12 = 0.0f; m->m21 = 0.0f; m->m22 = 1.0f;
        m->dx = 0.0f; m->dy = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *,
        FLOAT *ppd) noexcept override {
        *ppd = 1.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT x, FLOAT y,
        DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN *run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *, IUnknown *) noexcept override {
        if (g_factory2) {
            IDWriteColorGlyphRunEnumerator *en = nullptr;
            if (SUCCEEDED(g_factory2->TranslateColorGlyphRun(x, y, run,
                    nullptr, DWRITE_MEASURING_MODE_NATURAL, nullptr, 0,
                    &en))) {
                BOOL has = FALSE;
                while (en->MoveNext(&has) == S_OK && has) {
                    const DWRITE_COLOR_GLYPH_RUN *cr = nullptr;
                    if (SUCCEEDED(en->GetCurrentRun(&cr)) && cr) {
                        COLORREF col = RGB(255, 255, 255);
                        if (cr->runColor.a > 0.0f) {
                            col = RGB(
                                (int)(cr->runColor.r * 255.0f + 0.5f),
                                (int)(cr->runColor.g * 255.0f + 0.5f),
                                (int)(cr->runColor.b * 255.0f + 0.5f));
                        }
                        brt_->DrawGlyphRun(x, y,
                            DWRITE_MEASURING_MODE_NATURAL, &cr->glyphRun,
                            params_, col, nullptr);
                    }
                }
                en->Release();
                return S_OK;
            }
        }
        brt_->DrawGlyphRun(x, y, DWRITE_MEASURING_MODE_NATURAL, run,
            params_, RGB(255, 255, 255), nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT,
        const DWRITE_UNDERLINE *, IUnknown *) noexcept override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT,
        const DWRITE_STRIKETHROUGH *, IUnknown *) noexcept override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, FLOAT, FLOAT,
        IDWriteInlineObject *, BOOL, BOOL, IUnknown *) noexcept override {
        return S_OK;
    }

private:
    IDWriteBitmapRenderTarget *brt_;
    IDWriteRenderingParams *params_;
    ULONG ref_;
};

/* Formats cache: (family, size, bold) -- the family comes from the selected
 * GDI face so labels keep their configured look. */
struct ZanDwFormat {
    wchar_t family[64];
    float size;
    int bold;
    unsigned long long used;
    IDWriteTextFormat *fmt;
};
static const int ZAN_DW_FORMAT_CAP = 32;
static ZanDwFormat g_formats[ZAN_DW_FORMAT_CAP];
static int g_format_count = 0;
static unsigned long long g_format_clock = 0;

static IDWriteTextFormat *pick_format(const wchar_t *family, float size,
                                      int bold) {
    if (!family) family = L"";
    for (int i = 0; i < g_format_count; i++) {
        if (g_formats[i].size == size
            && g_formats[i].bold == bold
            && wcscmp(g_formats[i].family, family) == 0) {
            g_formats[i].used = ++g_format_clock;
            return g_formats[i].fmt;
        }
    }
    IDWriteTextFormat *fmt = nullptr;
    HRESULT hr = g_factory->CreateTextFormat(family, nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"",
        &fmt);
    if (FAILED(hr)) return nullptr;
    ZanDwFormat *slot = nullptr;
    if (g_format_count < ZAN_DW_FORMAT_CAP) {
        slot = &g_formats[g_format_count++];
    } else {
        slot = &g_formats[0];
        for (int i = 1; i < ZAN_DW_FORMAT_CAP; i++) {
            if (g_formats[i].used < slot->used) slot = &g_formats[i];
        }
    }
    if (slot->fmt) slot->fmt->Release();
    wcsncpy(slot->family, family, 63);
    slot->family[63] = L'\0';
    slot->size = size;
    slot->bold = bold;
    slot->used = ++g_format_clock;
    slot->fmt = fmt;
    return fmt;
}

/* 复用持久化 DWrite BitmapRenderTarget（按 64px 阶梯增长，上限 4096x512），
 * 避免每次文本光栅化都反复 GetDC + CreateBitmapRenderTarget + CreateDIBSection + Release。 */
static IDWriteBitmapRenderTarget *g_dw_brt = nullptr;
static int g_dw_brt_w = 0;
static int g_dw_brt_h = 0;
static unsigned char *g_dw_brt_bits = nullptr;
static int g_dw_brt_stride = 0;
static bool g_dw_brt_top_down = true;

static unsigned int g_dw_inv_alpha[256];
static bool g_dw_inv_ready = false;

static void zan_dw_init_inv_alpha() {
    if (g_dw_inv_ready) return;
    g_dw_inv_alpha[0] = 0;
    for (unsigned int a = 1; a < 256; a++) {
        g_dw_inv_alpha[a] = (255u * 65536u + a / 2u) / a;
    }
    g_dw_inv_ready = true;
}

static bool zan_dw_acquire_brt(int tw, int th,
                               IDWriteBitmapRenderTarget **out_brt,
                               bool *out_owned,
                               unsigned char **out_bits,
                               int *out_stride,
                               bool *out_top_down) {
    bool cacheable = (tw <= 4096 && th <= 512);
    if (cacheable) {
        if (!g_dw_brt || g_dw_brt_w < tw || g_dw_brt_h < th) {
            int cw = ((tw + 63) / 64) * 64;
            int ch = ((th + 63) / 64) * 64;
            if (cw < g_dw_brt_w) cw = g_dw_brt_w;
            if (ch < g_dw_brt_h) ch = g_dw_brt_h;
            if (cw > 4096) cw = 4096;
            if (ch > 512) ch = 512;
            HDC screen = GetDC(nullptr);
            IDWriteBitmapRenderTarget *nb = nullptr;
            HRESULT hr = g_gdi->CreateBitmapRenderTarget(screen, cw, ch, &nb);
            ReleaseDC(nullptr, screen);
            if (FAILED(hr) || !nb) return false;
            HDC mem = nb->GetMemoryDC();
            HBITMAP bmp = (HBITMAP)GetCurrentObject(mem, OBJ_BITMAP);
            BITMAP bm;
            if (!GetObjectW(bmp, sizeof(bm), &bm) || !bm.bmBits) {
                nb->Release();
                return false;
            }
            if (g_dw_brt) g_dw_brt->Release();
            g_dw_brt = nb;
            g_dw_brt_w = cw;
            g_dw_brt_h = ch;
            g_dw_brt_bits = (unsigned char *)bm.bmBits;
            g_dw_brt_stride = bm.bmWidthBytes;
            g_dw_brt_top_down = (bm.bmHeight > 0 || bm.bmWidthBytes >= 0);
        }
        size_t row_bytes = (size_t)tw * 4u;
        for (int y = 0; y < th; y++) {
            unsigned char *row = g_dw_brt_top_down
                ? g_dw_brt_bits + (size_t)y * (size_t)g_dw_brt_stride
                : g_dw_brt_bits + (size_t)(g_dw_brt_h - 1 - y) * (size_t)g_dw_brt_stride;
            memset(row, 0, row_bytes);
        }
        *out_brt = g_dw_brt;
        *out_owned = false;
        *out_bits = g_dw_brt_bits;
        *out_stride = g_dw_brt_stride;
        *out_top_down = g_dw_brt_top_down;
        return true;
    }
    HDC screen = GetDC(nullptr);
    IDWriteBitmapRenderTarget *brt = nullptr;
    HRESULT hr = g_gdi->CreateBitmapRenderTarget(screen, tw, th, &brt);
    ReleaseDC(nullptr, screen);
    if (FAILED(hr) || !brt) return false;
    HDC mem = brt->GetMemoryDC();
    HBITMAP bmp = (HBITMAP)GetCurrentObject(mem, OBJ_BITMAP);
    BITMAP bm;
    if (!GetObjectW(bmp, sizeof(bm), &bm) || !bm.bmBits) {
        brt->Release();
        return false;
    }
    *out_brt = brt;
    *out_owned = true;
    *out_bits = (unsigned char *)bm.bmBits;
    *out_stride = bm.bmWidthBytes;
    *out_top_down = (bm.bmHeight > 0 || bm.bmWidthBytes >= 0);
    return true;
}

extern "C" int zan_dw_text_width(const wchar_t *w, int len,
        const wchar_t *family, float size, int bold) {
    if (!zan_dw_init() || len <= 0) return 0;
    IDWriteTextFormat *fmt = pick_format(family, size, bold);
    if (!fmt) return 0;
    IDWriteTextLayout *lay = nullptr;
    if (FAILED(g_factory->CreateTextLayout(w, len, fmt, 8192.0f, 1024.0f,
            &lay)) || !lay) {
        return 0;
    }
    DWRITE_TEXT_METRICS tm;
    lay->GetMetrics(&tm);
    lay->Release();
    return (int)(tm.widthIncludingTrailingWhitespace + 0.5f);
}

/* Renders straight-alpha BGRA into a malloc'd buffer (caller frees with
 * free()). Returns 1 on success. */
extern "C" int zan_dw_render(const wchar_t *w, int len,
        const wchar_t *family, float size, int bold,
        unsigned char **out_px, int *out_w, int *out_h, int *out_advance) {
    *out_px = nullptr;
    if (!zan_dw_init() || len <= 0 || !g_gdi || !g_params) {
        return 0;
    }
    IDWriteTextFormat *fmt = pick_format(family, size, bold);
    if (!fmt) return 0;
    IDWriteTextLayout *lay = nullptr;
    HRESULT hrl = g_factory->CreateTextLayout(w, len, fmt, 8192.0f,
        1024.0f, &lay);
    if (FAILED(hrl) || !lay) {
        return 0;
    }
    DWRITE_TEXT_METRICS tm;
    lay->GetMetrics(&tm);
    DWRITE_LINE_METRICS lm[2];
    UINT32 got = 0;
    FLOAT baseline = size;
    FLOAT line_h = size * 2.0f;
    if (SUCCEEDED(lay->GetLineMetrics(lm, 2, &got)) && got > 0
        && lm[0].baseline > 0.0f) {
        baseline = lm[0].baseline;
        line_h = lm[0].height;
    }
    int tw = (int)tm.widthIncludingTrailingWhitespace + 3;
    int th = (int)line_h + 3;
    if (tw <= 4 || th <= 4 || tw > 8192 || th > 1024) {
        lay->Release();
        return 0;
    }
    IDWriteBitmapRenderTarget *brt = nullptr;
    bool owned = false;
    unsigned char *src_bits = nullptr;
    int stride = 0;
    bool top_down = true;
    if (!zan_dw_acquire_brt(tw, th, &brt, &owned, &src_bits, &stride, &top_down)) {
        lay->Release();
        return 0;
    }
    ZanDwRenderer renderer(brt, g_params);
    lay->Draw(nullptr, &renderer, 0.0f, baseline + 1.0f);

    unsigned char *buf = (unsigned char *)malloc((size_t)tw * (size_t)th * 4u);
    if (!buf) {
        if (owned) brt->Release();
        lay->Release();
        return 0;
    }
    int total_h = owned ? th : g_dw_brt_h;
    for (int y = 0; y < th; y++) {
        const unsigned char *row = top_down
            ? src_bits + (size_t)y * (size_t)stride
            : src_bits + (size_t)(total_h - 1 - y) * (size_t)stride;
        memcpy(buf + (size_t)y * (size_t)tw * 4u, row, (size_t)tw * 4u);
    }
    if (owned) brt->Release();
    lay->Release();
    /* Premultiplied BGRA -> straight BGRA (查表定点数倒数，免逐通道整数除法). */
    zan_dw_init_inv_alpha();
    int total_px = tw * th;
    for (int i = 0; i < total_px; i++) {
        unsigned char *px = buf + (size_t)i * 4u;
        unsigned char a = px[3];
        if (a > 0 && a < 255) {
            unsigned int inv = g_dw_inv_alpha[a];
            unsigned int b = ((unsigned int)px[0] * inv + 32768u) >> 16;
            unsigned int g = ((unsigned int)px[1] * inv + 32768u) >> 16;
            unsigned int r = ((unsigned int)px[2] * inv + 32768u) >> 16;
            px[0] = (unsigned char)(b > 255u ? 255u : b);
            px[1] = (unsigned char)(g > 255u ? 255u : g);
            px[2] = (unsigned char)(r > 255u ? 255u : r);
        }
    }
    *out_px = buf;
    *out_w = tw;
    *out_h = th;
    *out_advance = (int)(tm.widthIncludingTrailingWhitespace + 0.5f);
    return 1;
}
