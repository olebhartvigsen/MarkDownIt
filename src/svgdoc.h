#pragma once

// SvgDoc: holds a parsed SVG document and renders it with hybrid drawing.
//
// Direct2D cannot render SVG <text> elements (see
// https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support).
// Shapes go through ID2D1SvgDocument; text is extracted and drawn
// separately with DirectWrite.

#include <d2d1_3.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "svgtypes.h"

namespace svg {

class SvgDoc {
public:
    ~SvgDoc();

    // Move-only: the type owns a COM reference. Copying would hand the same
    // ID2D1SvgDocument* to two owners whose destructors both Release it
    // (the vector-backed SVG cache relies on moving, so forbid copies).
    SvgDoc() = default;
    SvgDoc(SvgDoc&& o) noexcept
        : doc_(o.doc_), texts_(std::move(o.texts_)),
          width_(o.width_), height_(o.height_) {
        o.doc_ = nullptr;
        o.width_ = 0.0f;
        o.height_ = 0.0f;
    }
    SvgDoc& operator=(SvgDoc&& o) noexcept {
        if (this != &o) {
            Release();
            doc_ = o.doc_; texts_ = std::move(o.texts_);
            width_ = o.width_; height_ = o.height_;
            o.doc_ = nullptr; o.width_ = 0.0f; o.height_ = 0.0f;
        }
        return *this;
    }
    SvgDoc(const SvgDoc&) = delete;
    SvgDoc& operator=(const SvgDoc&) = delete;

    // Parse an SVG string. Returns false on failure.
    bool Load(ID2D1DeviceContext5* ctx, const std::string& xml);

    // Natural size from width/height or viewBox.
    float Width() const { return width_; }
    float Height() const { return height_; }

    // Draw the document scaled into (x, y, w, h), preserving aspect ratio.
    void Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
              float x, float y, float w, float h);

    void Release();

private:
    void DrawTexts(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                   const D2D1_MATRIX_3X2_F& docTransform);

    ID2D1SvgDocument* doc_ = nullptr;
    std::vector<TextRun> texts_;
    float width_ = 0.0f;
    float height_ = 0.0f;
};

}  // namespace svg
