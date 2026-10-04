#pragma once

#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "dom.h"
#include "theme.h"
#include "layoutcache.h"
#include "svgdoc.h"
#include "caret.h"
#include "zoommodel.h"
#include "mermaid/layout_cache.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Init(IDWriteFactory* dw);
    void Release();

    // Zoom model: default 100%, clamped to 25%..400%, step x1.25.
    // The constants and clamp live in zoommodel.h (namespace zoom)
    // so the model is unit-testable headless; the Renderer::k* names
    // below are aliases so callers keep a single spelling.
    //
    // Zoom is a GLOBAL view setting, not per-document state: it is
    // never reset when a document opens or closes, so every file shares
    // the factor within a session. AppWindow persists the factor in
    // AppSettings (HKCU registry) on every change and restores it in
    // OnCreate before the renderer is first initialized, so the last
    // zoom survives a restart as well.
    static constexpr float kMinZoom     = zoom::kMinZoom;
    static constexpr float kMaxZoom     = zoom::kMaxZoom;
    static constexpr float kDefaultZoom = zoom::kDefaultZoom;

    // Set a specific zoom factor. The value is clamped to
    // [kMinZoom, kMaxZoom] before it is stored.
    void SetZoom(float z);
    float GetZoom() const { return zoom_; }
    float Zoom() const { return zoom_; }  // legacy alias for GetZoom
    void SetWrap(bool w);
    bool Wrap() const { return wrapEnabled_; }

    // Content width mode: 0=Standard(800), 1=960, 2=1600, 3=Full width.
    void SetContentWidthMode(int mode);
    int  ContentWidthMode() const { return contentWidthMode_; }

    // Width of the content column in DIPs at the current zoom, which is
    // the horizontal scroll range. Mode 3 has no cap, so it reports the
    // viewport's own width and therefore never scrolls horizontally.
    float ContentWidthDip() const { return ComputeMetrics().maxContentWidth; }

    // True when the document on screen is a standalone .svg file, not
    // markdown. Only then is a ```svg fence drawn as a picture; inside a
    // .md file a ```svg fence is code the author wrote, so it stays code.
    void SetStandaloneSvg(bool v) { standaloneSvg_ = v; }
    bool StandaloneSvg() const { return standaloneSvg_; }

    void SetLayoutCache(LayoutCache* cache) { cache_ = cache; }

    // Set pointer to the source text for offset calculations.
    void SetSourceText(const std::string* src) { srcText_ = src; }
    const std::string* SourceText() const { return srcText_; }
    // Draw the document. scrollY is the vertical offset in DIPs.
    // widthDip is the client width in DIPs.
    // Returns the total rendered height in DIPs (unscrolled).
    // scrollX scrolls the content column horizontally; it is zero unless
    // the column is wider than the viewport, which happens at high zoom.
    float Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                 const Document& doc, float widthDip, float scrollY,
                 float topOffsetDip, float scrollX = 0.0f,
                 const Selection* sel = nullptr);

    // Measure the total content height in DIPs without drawing.
    // Use this when only the scrollbar range needs updating.
    float Measure(IDWriteFactory* dw, const Document& doc, float widthDip,
                     float topOffsetDip);

    // Draw the raw markdown source with monospace font and word wrap.
    // Returns the total rendered height in DIPs.
    float RenderSourceView(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                           const std::string& src, float widthDip,
                           float scrollY, float topOffsetDip,
                           float scrollX = 0.0f,
                           const Selection* sel = nullptr);
    // Measure the source view height without drawing.
    float MeasureSourceView(IDWriteFactory* dw, const std::string& src,
                            float widthDip, float topOffsetDip);

private:
    IDWriteTextFormat* body_fmt_ = nullptr;
    IDWriteTextFormat* num_fmt_ = nullptr;     // seq autonumber (12px)
    IDWriteTextFormat* code_fmt_ = nullptr;      // monospace for code blocks
    IDWriteTextFormat* mermaid_measure_fmt_ = nullptr;  // unzoomed DirectWrite metrics
    IDWriteTextFormat* heading_fmt_[7] = {};
    float zoom_ = kDefaultZoom;  // 100%
    bool wrapEnabled_ = true;
    bool standaloneSvg_ = false;
    int  contentWidthMode_ = 0;  // 0=Standard(800), 1=960, 2=1600, 3=Full
    LayoutCache* cache_ = nullptr;
    const std::string* srcText_ = nullptr;  // source text for offset calc
    ID2D1DeviceContext5* d2d_ctx5_ = nullptr;  // may be null
    mutable mermaid::MermaidLayoutCache mermaid_layout_cache_;

public:
    void SetD2DDeviceContext5(ID2D1DeviceContext5* ctx) { d2d_ctx5_ = ctx; }
    void ClearSvgCache();
    svg::SvgDoc* GetSvgDoc(const Node& n, float availW);

private:
    // SVG document cache: keyed by srcOffset.
    struct SvgCacheEntry {
        uint32_t srcOffset = 0;
        svg::SvgDoc doc;
        float lastWidth = 0;
        float lastHeight = 0;
    };
    std::vector<SvgCacheEntry> svg_cache_;

    // Decoded inline-image cache: keyed by the URL/alt string, so a remote
    // image is fetched once per document view instead of once per paint
    // frame (every scroll tick used to re-download it on the UI thread).
    struct InlineImageCacheEntry {
        std::string url;
        ID2D1Bitmap* bmp = nullptr;
        // A URL that already failed to load or decode. Kept so a repaint
        // does not retry it: the fetch is synchronous and on the paint
        // thread, so an unreachable URL in the document would otherwise be
        // re-requested on every scroll tick and every mouse-move repaint.
        bool failed = false;
        // SVG images cache their source text rather than a bitmap, so the
        // same entry serves the load once and every later repaint.
        std::string svgText;
    };
    std::vector<InlineImageCacheEntry> img_cache_;

    void DrawSvgBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                      const Node& n, float x, float y, float width,
                      float& outH, const Selection* sel);
    float MeasureSvgBlock(IDWriteFactory* dw, const Node& n,
                          float x, float width);

    LayoutMetrics ComputeMetrics() const;
    static float GapForTransition(BlockKind prev, BlockKind cur,
                                  BlockKind next, int curDepth,
                                  int prevDepth, const LayoutMetrics& m);

    std::u16string ToUtf16(const std::u32string& s32);
    void DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Node& n, float x, float y, float width, float& outH,
                       const Selection* sel = nullptr);
    void DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width);
    void DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                   const Node& n, float x, float y, float width, float& outH,
                   const Selection* sel = nullptr);
    float MeasureTable(IDWriteFactory* dw, const Node& n,
                       float x, float width);
    void DrawMermaidBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                          const Node& n, float x, float y, float width);
    float MeasureMermaidBlock(IDWriteFactory* dw, const Node& n, float width) const;
    std::shared_ptr<mermaid::LaidOutFlowchart> GetMermaidLayout(
        IDWriteFactory* dw, const Node& n) const;
    void DrawMermaidPieBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                             const Node& n, float x, float y, float width);
    void DrawMermaidSequenceBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                  const Node& n, float x, float y, float width);
};
