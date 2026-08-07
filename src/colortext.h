#pragma once

// ColorTextRenderer: custom IDWriteTextRenderer that draws text with
// per-range colors via SetDrawingEffect. Used to render links in blue.
// Non-link ranges use the default black brush.

#include <dwrite.h>
#include <d2d1.h>

class ColorTextRenderer : public IDWriteTextRenderer {
public:
    ColorTextRenderer(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* defaultBrush,
                       ID2D1SolidColorBrush* linkBrush);
    ~ColorTextRenderer() override;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDWriteTextRenderer
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* ctx, FLOAT baselineX, FLOAT baselineY,
        DWRITE_MEASURING_MODE mode, DWRITE_GLYPH_RUN const* run,
        DWRITE_GLYPH_RUN_DESCRIPTION const* desc, IUnknown* effect) override;
    HRESULT STDMETHODCALLTYPE DrawUnderline(void* ctx, FLOAT baselineX, FLOAT baselineY,
        DWRITE_UNDERLINE const* underline, IUnknown* effect) override;
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* ctx, FLOAT baselineX, FLOAT baselineY,
        DWRITE_STRIKETHROUGH const* strikethrough, IUnknown* effect) override;
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* ctx, FLOAT originX, FLOAT originY,
        IDWriteInlineObject* obj, BOOL isSideways, BOOL isRightToLeft,
        IUnknown* effect) override;

    // IDWritePixelSnapping
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void* ctx, BOOL* isDisabled) override;
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void* ctx, DWRITE_MATRIX* transform) override;
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* ctx, FLOAT* ppd) override;

private:
    ID2D1RenderTarget*     rt_;
    ID2D1SolidColorBrush*  defaultBrush_;
    ID2D1SolidColorBrush*  linkBrush_;
};
