#pragma once

#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <map>
#include <string>
#include <vector>
#include "dom.h"
#include "theme.h"
#include "layoutcache.h"
#include "svgdoc.h"
#include "caret.h"
#include "zoommodel.h"
#include "searchreplace.h"
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

    // Column width used when word wrap is off. Render then lays the text
    // out on one very wide line and lets the user scroll to its end.
    static constexpr float kNoWrapContentWidthDip = 10000.0f;

    // Set a specific zoom factor. The value is clamped to
    // [kMinZoom, kMaxZoom] before it is stored.
    // Line height of fmt per DWRITE_FONT_METRICS (ascent+descent+
    // lineGap) scaled to the format's size, cached. This is the natural
    // font line height the cursor-blinking guide requires for the
    // caret, distinct from both the em size and the rendered line box
    // (the app's line spacing multiplier is deliberately excluded).
    float LineHeightOf(IDWriteTextFormat* fmt);
    void SetZoom(float z);
    float GetZoom() const { return zoom_; }
    float Zoom() const { return zoom_; }  // legacy alias for GetZoom
    void SetWrap(bool w);
    bool Wrap() const { return wrapEnabled_; }

    // Content width mode: 0=Standard(800), 1=960, 2=1600, 3=Full width.
    void SetContentWidthMode(int mode);
    int  ContentWidthMode() const { return contentWidthMode_; }

    // Width of the drawn content column in DIPs at the current zoom. This
    // is the horizontal scroll range, so it has to describe what Render
    // actually paints: the smaller of the capped column width and the
    // width the viewport leaves after padding. The uncapped width mode
    // therefore reports the viewport width and never scrolls sideways.
    float ContentWidthDip(float viewportWidthDip) const;

    // Column width at 100% zoom for the current width mode, which is the
    // denominator for fit width. Zero means the mode sets no column width,
    // so the content already fills the viewport and fit width does nothing.
    float BaseContentWidthDip() const;

    // True when the document on screen is a standalone .svg file, not
    // markdown. Only then is a ```svg fence drawn as a picture; inside a
    // .md file a ```svg fence is code the author wrote, so it stays code.
    void SetStandaloneSvg(bool v) { standaloneSvg_ = v; }
    bool StandaloneSvg() const { return standaloneSvg_; }

    void SetLayoutCache(LayoutCache* cache) { cache_ = cache; }

    // Search-result highlighting. The matches are DOCUMENT ranges, given as
    // source byte offsets exactly like Selection::Start()/Length(), never as
    // screen coordinates: zoom, scroll and re-layout therefore cannot change
    // which text is highlighted. currentIndex is the position of the match
    // find is standing on, or -1 when there is no current match. Passing a
    // null pointer clears the highlighting.
    void SetSearchMatches(const std::vector<TextMatch>* matches,
                          int currentIndex);
    void ClearSearchMatches() { SetSearchMatches(nullptr, -1); }

    // Marker (highlight annotation) layer. Same contract as the search
    // highlights: the pointer is borrowed and outlives the paint, and the
    // visible flag hides the layer without dropping any data.
    void SetMarkers(const std::vector<TextMatch>* markers, bool visible);
    const std::vector<TextMatch>* SearchMatches() const {
        return searchMatches_;
    }
    int SearchCurrentIndex() const { return searchCurrentIndex_; }

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
    // Search results to highlight. Borrowed, not owned: the find state owns
    // the vector and outlives the paint that reads it. Null means no
    // highlighting at all, which keeps the empty case free of work.
    const std::vector<TextMatch>* searchMatches_ = nullptr;
    int searchCurrentIndex_ = -1;
    // Marker ranges to paint. Borrowed like the search matches, and drawn
    // before them so the find hit and the text stay readable on top.
    const std::vector<TextMatch>* markerRanges_ = nullptr;
    bool markersVisible_ = false;
    // Cache of the line height per text format, in DIP at the current
    // zoom, used for caret sizing (cursor-blinking guide). Keyed by
    // format pointer: formats are re-created on zoom change, so the
    // pointer acts as the cache key and a miss refills it.
    std::map<const void*, float> lineHeightCache_;
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

    // True when there is at least one match to paint. The four paint paths
    // check this before creating any brush, so a closed find bar costs
    // nothing per frame.
    bool HasSearchMatches() const {
        return searchMatches_ && !searchMatches_->empty();
    }

    // True when the marker layer has anything to paint. The paint paths
    // check this before creating the marker brushes.
    bool HasMarkers() const {
        return markersVisible_ && markerRanges_ && !markerRanges_->empty();
    }

    // Paint the search-match fills for one block of text, using the same
    // line-by-line hit test as the selection highlight so the fill follows
    // wrapping, zoom and scroll. u16ToSrc maps each UTF-16 index of the block
    // layout to its source byte offset; blockStart/blockEnd are the source
    // byte range the block covers; matches outside it are skipped. Other
    // matches get matchBrush, the match at searchCurrentIndex_ gets
    // currentBrush. The mapping is read with a binary search when it is
    // non-decreasing, with the linear scan kept as the fallback.
    void FillMatchHighlights(ID2D1RenderTarget* rt,
                             IDWriteTextLayout* layout,
                             ID2D1SolidColorBrush* matchBrush,
                             ID2D1SolidColorBrush* currentBrush,
                             const std::vector<uint32_t>& u16ToSrc,
                             UINT32 u16Len,
                             uint32_t blockStart, uint32_t blockEnd,
                             float originX, float originY,
                             float scrollY, float viewportH);

    // Paint the marker fills for one block. Same shape as the search
    // highlights: one brush for the fill and one for the thin bottom line
    // that keeps a mark off colour alone. There is no current-marker
    // concept, every mark gets the same fill.
    void FillMarkerHighlights(ID2D1RenderTarget* rt,
                             IDWriteTextLayout* layout,
                             ID2D1SolidColorBrush* fillBrush,
                             ID2D1SolidColorBrush* lineBrush,
                             const std::vector<uint32_t>& u16ToSrc,
                             UINT32 u16Len,
                             uint32_t blockStart, uint32_t blockEnd,
                             float originX, float originY,
                             float scrollY, float viewportH);

    void DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Node& n, float x, float y, float width, float& outH,
                       const Selection* sel = nullptr, float scrollY = 0.0f);
    void DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width);
    void DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                   const Node& n, float x, float y, float width, float& outH,
                   const Selection* sel = nullptr, float scrollY = 0.0f);
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
