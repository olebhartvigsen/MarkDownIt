#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "theme.h"
#include "layoutcache.h"
#include "caret.h"

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

    void SetLayoutCache(LayoutCache* cache) { cache_ = cache; }

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

private:
    IDWriteTextFormat* body_fmt_ = nullptr;
    IDWriteTextFormat* code_fmt_ = nullptr;      // monospace for code blocks
    IDWriteTextFormat* heading_fmt_[7] = {};
    float zoom_ = 1.0f;
    bool wrapEnabled_ = true;
    LayoutCache* cache_ = nullptr;

    LayoutMetrics ComputeMetrics() const;
    static float GapForTransition(BlockKind prev, BlockKind cur,
                                  BlockKind next, int curDepth,
                                  int prevDepth, const LayoutMetrics& m);

    std::u16string ToUtf16(const std::u32string& s32);
    void DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Node& n, float x, float y, float width, float& outH);
    void DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width);
    void DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                   const Node& n, float x, float y, float width, float& outH);
    float MeasureTable(IDWriteFactory* dw, const Node& n,
                       float x, float width);
};
