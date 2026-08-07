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

    // Draw the document. scrollY is the vertical offset in DIPs.
    // widthDip is the client width in DIPs.
    // Returns the total rendered height in DIPs (unscrolled).
    float Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                 const Document& doc, float widthDip, float scrollY);

    // Measure the total content height in DIPs without drawing.
    // Use this when only the scrollbar range needs updating.
    float Measure(IDWriteFactory* dw, const Document& doc, float widthDip);

private:
    IDWriteTextFormat* body_fmt_ = nullptr;
    IDWriteTextFormat* code_fmt_ = nullptr;      // monospace for code blocks
    IDWriteTextFormat* heading_fmt_[7] = {};

    std::u16string ToUtf16(const std::u32string& s32);
    void DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Node& n, float x, float y, float width, float& outH);
    void DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width);
};
