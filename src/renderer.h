#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"

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

    // Draw the document. scrollY is the vertical offset in DIPs.
    // widthDip is the client width in DIPs.
    // Returns the total rendered height in DIPs (unscrolled).
    float Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                 const Document& doc, float widthDip, float scrollY,
                 float topOffsetDip);

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

    std::u16string ToUtf16(const std::u32string& s32);
    void DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Node& n, float x, float y, float width, float& outH);
    void DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width);
    void DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                   const Node& n, float x, float y, float width, float& outH);
};
