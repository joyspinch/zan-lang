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

static FILE *dbg();
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
        FILE *f = dbg();
        if (f) { fprintf(f, "glyphrun glyphCount=%u factory2=%d' + chr(92) + 'n",
            (unsigned)run->glyphCount, (int)(g_factory2 != nullptr)); fflush(f); }
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
    IDWriteTextFormat *fmt;
};
static ZanDwFormat g_formats[8];
static int g_format_count = 0;

static IDWriteTextFormat *pick_format(const wchar_t *family, float size,
                                      int bold) {
    if (!family) family = L"";
    for (int i = 0; i < g_format_count; i++) {
        if (g_formats[i].size == size
            && g_formats[i].bold == bold
            && wcscmp(g_formats[i].family, family) == 0) {
            return g_formats[i].fmt;
        }
    }
    IDWriteTextFormat *fmt = nullptr;
    HRESULT hr = g_factory->CreateTextFormat(family, nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"",
        &fmt);
    if (FAILED(hr)) return nullptr;
    ZanDwFormat *slot = (g_format_count < 8)
        ? &g_formats[g_format_count++]
        : &g_formats[0];
    if (slot->fmt) slot->fmt->Release();
    wcsncpy(slot->family, family, 63);
    slot->family[63] = L'\0';
    slot->size = size;
    slot->bold = bold;
    slot->fmt = fmt;
    return fmt;
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
static FILE *dbg() {
    static FILE *f = nullptr;
    if (!f) { f = fopen("emo_dw.log", "a"); }
    return f;
}

extern "C" int zan_dw_render(const wchar_t *w, int len,
        const wchar_t *family, float size, int bold,
        unsigned char **out_px, int *out_w, int *out_h, int *out_advance) {
    *out_px = nullptr;
    FILE *f = dbg();
    if (f) fprintf(f, "render enter len=%d init_ok=%d gdi=%d params=%d\n",
        len, (int)(g_factory != nullptr), (int)(g_gdi != nullptr),
        (int)(g_params != nullptr));
    if (f) fflush(f);
    if (!zan_dw_init() || len <= 0 || !g_gdi || !g_params) {
        if (f) fprintf(f, "render bail init\n");
        return 0;
    }
    IDWriteTextFormat *fmt = pick_format(family, size, bold);
    if (!fmt) return 0;
    IDWriteTextLayout *lay = nullptr;
    HRESULT hrl = g_factory->CreateTextLayout(w, len, fmt, 8192.0f,
        1024.0f, &lay);
    if (FAILED(hrl) || !lay) {
        if (f) fprintf(f, "layout FAILED hr=%08x\n", (unsigned)hrl);
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
    if (f) fprintf(f, "metrics tw=%d th=%d baseline=%.1f' + chr(92) + 'n", tw, th, baseline);
    if (f) fflush(f);
    if (tw <= 4 || th <= 4 || tw > 8192 || th > 1024) {
        lay->Release();
        return 0;
    }
    HDC screen = GetDC(nullptr);
    IDWriteBitmapRenderTarget *brt = nullptr;
    HRESULT hr2 = g_gdi->CreateBitmapRenderTarget(screen, tw, th, &brt);
    HRESULT hr = hr2;
    ReleaseDC(nullptr, screen);
    if (FAILED(hr) || !brt) {
        if (f) fprintf(f, "brt FAILED hr=%08x\n", (unsigned)hr);
        lay->Release();
        return 0;
    }
    ZanDwRenderer renderer(brt, g_params);
    lay->Draw(nullptr, &renderer, 0.0f, baseline + 1.0f);

    /* Read the render target's 32bpp premultiplied BGRA DIB section: the
     * alpha channel only survives a direct bits read -- GetDIBits with
     * BI_RGB zeroes it. */
    HDC mem = brt->GetMemoryDC();
    HBITMAP bmp = (HBITMAP)GetCurrentObject(mem, OBJ_BITMAP);
    BITMAP bm;
    if (!GetObjectW(bmp, sizeof(bm), &bm) || !bm.bmBits) {
        brt->Release();
        lay->Release();
        return 0;
    }
    int stride = bm.bmWidthBytes;
    unsigned char *src_bits = (unsigned char *)bm.bmBits;
    /* DWrite creates a top-down DIB; assert defensively via the sign bit. */
    bool top_down = bm.bmHeight > 0 || stride >= 0;
    unsigned char *buf = (unsigned char *)malloc((size_t)tw * th * 4);
    if (!buf) {
        brt->Release();
        lay->Release();
        return 0;
    }
    for (int y = 0; y < th; y++) {
        const unsigned char *row = top_down
            ? src_bits + (size_t)y * stride
            : src_bits + (size_t)(th - 1 - y) * stride;
        memcpy(buf + (size_t)y * tw * 4, row, (size_t)tw * 4);
    }
    brt->Release();
    lay->Release();
    /* Premultiplied BGRA -> straight BGRA. */
    {
        FILE *f = dbg();
        int ink = 0, maxa = 0;
        for (int i = 0; i < tw * th; i++) {
            unsigned char a = buf[i * 4 + 3];
            if (a > 0) ink++;
            if (a > maxa) maxa = a;
        }
        if (f) { fprintf(f, "readback ink=%d maxa=%d' + chr(92) + 'n", ink, maxa); fflush(f); }
    }
    for (int i = 0; i < tw * th; i++) {
        unsigned char a = buf[i * 4 + 3];
        if (a > 0 && a < 255) {
            for (int c = 0; c < 3; c++) {
                unsigned v = (unsigned)buf[i * 4 + c] * 255u / a;
                buf[i * 4 + c] = (unsigned char)(v > 255 ? 255 : v);
            }
        }
    }
    *out_px = buf;
    *out_w = tw;
    *out_h = th;
    *out_advance = (int)(tm.widthIncludingTrailingWhitespace + 0.5f);
    return 1;
}

/* 临时诊断：win_run_tile 的路由判定落文件，定位后删除。 */
extern "C" int zan_dw_probe_log(int len, int emoji, int missing) {
    FILE *f = fopen("emo_dw.log", "a");
    if (f) {
        fprintf(f, "run_tile len=%d emoji=%d missing=%d\n", len, emoji,
            missing);
        fclose(f);
    }
    return 0;
}
