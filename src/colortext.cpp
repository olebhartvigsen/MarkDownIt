#include "colortext.h"

ColorTextRenderer::ColorTextRenderer(ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* defaultBrush, ID2D1SolidColorBrush* linkBrush)
    : rt_(rt), defaultBrush_(defaultBrush), linkBrush_(linkBrush) {}

ColorTextRenderer::~ColorTextRenderer() {}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::QueryInterface(REFIID riid, void** ppv) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextRenderer) ||
        riid == __uuidof(IDWritePixelSnapping)) {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ColorTextRenderer::AddRef() { return 1; }
ULONG STDMETHODCALLTYPE ColorTextRenderer::Release() { return 1; }

HRESULT STDMETHODCALLTYPE ColorTextRenderer::DrawGlyphRun(
    void* ctx, FLOAT baselineX, FLOAT baselineY, DWRITE_MEASURING_MODE mode,
    DWRITE_GLYPH_RUN const* run, DWRITE_GLYPH_RUN_DESCRIPTION const* desc,
    IUnknown* effect) {
    // If effect is set, this range had SetDrawingEffect called (links).
    ID2D1Brush* brush = effect ? static_cast<ID2D1Brush*>(linkBrush_) : defaultBrush_;
    rt_->DrawGlyphRun(D2D1::Point2F(baselineX, baselineY), run, brush, mode);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::DrawUnderline(
    void* ctx, FLOAT baselineX, FLOAT baselineY, DWRITE_UNDERLINE const* underline,
    IUnknown* effect) {
    ID2D1Brush* brush = effect ? static_cast<ID2D1Brush*>(linkBrush_) : defaultBrush_;
    D2D1_RECT_F r = D2D1::RectF(baselineX, baselineY + underline->offset,
                                baselineX + underline->width,
                                baselineY + underline->offset + underline->thickness);
    rt_->FillRectangle(r, brush);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::DrawStrikethrough(
    void* ctx, FLOAT baseX, FLOAT baseY, DWRITE_STRIKETHROUGH const* st, IUnknown* effect) {
    ID2D1Brush* brush = effect ? static_cast<ID2D1Brush*>(linkBrush_) : defaultBrush_;
    D2D1_RECT_F r = D2D1::RectF(baseX, baseY + st->offset, baseX + st->width,
                                baseY + st->offset + st->thickness);
    rt_->FillRectangle(r, brush);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::DrawInlineObject(
    void* ctx, FLOAT x, FLOAT y, IDWriteInlineObject* obj, BOOL sideways,
    BOOL rtl, IUnknown* effect) {
    return S_OK;  // no inline objects for now
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::IsPixelSnappingDisabled(void* ctx, BOOL* disabled) {
    *disabled = FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::GetCurrentTransform(void* ctx, DWRITE_MATRIX* transform) {
    rt_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(transform));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ColorTextRenderer::GetPixelsPerDip(void* ctx, FLOAT* ppd) {
    FLOAT dpiX, dpiY;
    rt_->GetDpi(&dpiX, &dpiY);
    *ppd = dpiX / 96.0f;
    return S_OK;
}
