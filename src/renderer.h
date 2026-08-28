#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "theme.h"
#include "layoutcache.h"
#include "caret.h"
#include "mermaidlayout.h"
#include "diagramcache.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Init(IDWriteFactory* dw);
    void Release();
    void SetZoom(float z);
    float Zoom() const { return zoom_; }
    void SetWrap(bool w);
    bool Wrap() const { return wrapEnabled_; }

    // Content width mode: 0=Standard(800), 1=960, 2=1600, 3=Full width.
    void SetContentWidthMode(int mode);
    int  ContentWidthMode() const { return contentWidthMode_; }

    void SetLayoutCache(LayoutCache* cache) { cache_ = cache; }

    // D2D factory for path geometry creation (diamonds, arrows).
    void SetD2DFactory(ID2D1Factory* f) { d2d_factory_ = f; }

    // Diagram cache for parsed/laid-out mermaid diagrams.
    void SetDiagramCache(mermaid::DiagramCache* c) { diagram_cache_ = c; }

    // Measure text width using the code font, for mermaid layout sizing.
    static float MeasureTextWidth(const std::string& text, void* ctx);

    // Set pointer to the source text for offset calculations.
    void SetSourceText(const std::string* src) { srcText_ = src; }
    const std::string* SourceText() const { return srcText_; }
    // Draw the document. scrollY is the vertical offset in DIPs.
    // widthDip is the client width in DIPs.
    // Returns the total rendered height in DIPs (unscrolled).
    float Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                 const Document& doc, float widthDip, float scrollY,
                 float topOffsetDip,
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
                           const Selection* sel);
    // Measure the source view height without drawing.
    float MeasureSourceView(IDWriteFactory* dw, const std::string& src,
                            float widthDip, float topOffsetDip);

private:
    IDWriteTextFormat* body_fmt_ = nullptr;
    IDWriteTextFormat* code_fmt_ = nullptr;      // monospace for code blocks
    IDWriteTextFormat* heading_fmt_[7] = {};
    float zoom_ = 0.8f;  // default: one zoom-out step smaller
    bool wrapEnabled_ = true;
    int  contentWidthMode_ = 0;  // 0=Standard(800), 1=960, 2=1600, 3=Full
    LayoutCache* cache_ = nullptr;
    ID2D1Factory* d2d_factory_ = nullptr;
    mermaid::DiagramCache* diagram_cache_ = nullptr;
    IDWriteFactory* dw_factory_ = nullptr;  // for text measurement in layout
    const std::string* srcText_ = nullptr;  // source text for offset calc

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

    // Draw a mermaid diagram layout.
    void DrawDiagram(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                     const mermaid::Layout& layout,
                     float x, float y, float width, float& outH);
    // Measure a mermaid diagram layout (no drawing).
    float MeasureDiagram(const mermaid::Layout& layout,
                          float x, float width);
};
