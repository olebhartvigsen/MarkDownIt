#pragma once

// Renderer: draws the parsed Document into a Direct2D render target
// using DirectWrite for text layout. Task 6 v0 handles only headings
// and paragraphs; code blocks, lists, blockquotes, and inline spans
// come in later tasks.

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Create the IDWriteTextFormat objects. Call once after DWriteCreateFactory.
    bool Init(IDWriteFactory* dw);
    void Release();

    // Draw the document into rt. widthDip is the client width in DIPs.
    // Returns the total rendered height in DIPs (so the caller can set
    // scroll limits in Task 8).
    float Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                 const Document& doc, float widthDip);

private:
    // Text formats: index 0 unused, 1..6 for heading levels.
    IDWriteTextFormat* body_fmt_ = nullptr;
    IDWriteTextFormat* heading_fmt_[7] = {};

    // UTF-32 (DOM) to UTF-16 (DirectWrite WCHAR).
    std::u16string ToUtf16(const std::u32string& s32);
};
