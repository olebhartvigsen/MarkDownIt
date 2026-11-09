#pragma once

// The Direct2D render target behind the RenderTarget seam. The macOS
// twin is CoreGraphicsTarget in src/macos/cg_target_mac.h. The wrapped
// ID2D1RenderTarget is borrowed and never released here, and every
// method draws in DIPs, the space the seam already speaks.

#include <map>
#include <string>
#include <tuple>

#include "platform/seams.h"

// windows.h turns DrawText into DrawTextW through a macro, which
// rips the seam's virtual method out of the class and leaves its
// definition as a free function. The macro brings nothing this file
// uses, so it goes away right here, before any member is declared.
// The .cpp repeats the removal after its own platform includes.
#ifdef DrawText
#undef DrawText
#endif

struct ID2D1RenderTarget;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace platform {

class D2DTarget final : public RenderTarget {
public:
    explicit D2DTarget(ID2D1RenderTarget* target);
    ~D2DTarget() override;

    D2DTarget(const D2DTarget&) = delete;
    D2DTarget& operator=(const D2DTarget&) = delete;

    void FillRect(const Rect& rect, const Color& color) override;
    void FillRoundedRect(const Rect& rect, float radius, const Color& color) override;
    void FillRoundedRectSides(const Rect& rect, float radius, const Color& color, bool roundLeft, bool roundRight) override;
    void StrokeRect(const Rect& rect, const Color& color, float lineWidth) override;
    void StrokeRoundedRect(const Rect& rect, float radius, const Color& color, float lineWidth) override;
    void StrokeLine(float x0, float y0, float x1, float y1, const Color& color, float lineWidth) override;
    void DrawText(const std::string& utf8, const FontSpec& font, float x, float baselineY, const Color& color) override;
    void DrawBitmap(BitmapHandle image, const Rect& rect) override;
    void FillEllipse(const Rect& rect, const Color& color) override;
    void StrokeEllipse(const Rect& rect, const Color& color, float lineWidth) override;
    void FillPolygon(const std::vector<Point>& points, const Color& color) override;
    void StrokePolygon(const std::vector<Point>& points, const Color& color, float lineWidth, bool closed) override;
    void FillPieSlice(const Rect& rect, float startAngle, float endAngle, const Color& color) override;
    void PushClip(const Rect& rect) override;
    void PopClip() override;

private:
    // One brush per call, then Released, the renderer.cpp lifecycle.
    ID2D1SolidColorBrush* BrushFor(const Color& color) const;

    // A cached format per family, size, weight and style, keyed as ints
    // so this header needs no dwrite.h, like the DWriteEngine's cache.
    IDWriteTextFormat* FormatFor(const FontSpec& font);

    // Drawn into, never owned or released by this class.
    ID2D1RenderTarget* target_ = nullptr;
    IDWriteFactory* write_factory_ = nullptr;
    std::map<std::tuple<std::wstring, float, int, int>, IDWriteTextFormat*> formats_;
};

}  // namespace platform
