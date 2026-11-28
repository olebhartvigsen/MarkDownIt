#pragma once

// A RenderTarget that shifts every coordinate by a fixed offset.
//
// The display list and the block painters work in content column
// coordinates, with the scroll and the column's left edge not applied. A
// target draws in view coordinates. Windows used to express the difference
// with SetTransform and the mac with a Core Graphics context translate, and
// both were platform calls in a painter. The seam has no transform, and it
// does not need one: the shift belongs to the coordinates, so this adapter
// applies it there, once, for every drawing call.
//
// It is pure seam code, no platform header, and both renderers use the same
// class. DrawLayout is forwarded rather than left to the base default,
// because the base default draws a block run by run and would lose a native
// layout's own breaks and alignment.

#include "platform/seams.h"

namespace platform {

class OffsetTarget final : public RenderTarget {
public:
    OffsetTarget(RenderTarget* inner, float dx, float dy)
        : inner_(inner), dx_(dx), dy_(dy) {}

    void FillRect(const Rect& rect, const Color& color) override {
        inner_->FillRect(Shift(rect), color);
    }
    void FillRects(const std::vector<Rect>& rects,
                   const Color& color) override {
        std::vector<Rect> shifted;
        shifted.reserve(rects.size());
        for (const Rect& rect : rects) shifted.push_back(Shift(rect));
        inner_->FillRects(shifted, color);
    }
    void FillRoundedRect(const Rect& rect, float radius,
                         const Color& color) override {
        inner_->FillRoundedRect(Shift(rect), radius, color);
    }
    void FillRoundedRectSides(const Rect& rect, float radius,
                              const Color& color, bool roundLeft,
                              bool roundRight) override {
        inner_->FillRoundedRectSides(Shift(rect), radius, color, roundLeft,
                                     roundRight);
    }
    void StrokeRect(const Rect& rect, const Color& color,
                    float lineWidth) override {
        inner_->StrokeRect(Shift(rect), color, lineWidth);
    }
    void StrokeRoundedRect(const Rect& rect, float radius,
                           const Color& color, float lineWidth) override {
        inner_->StrokeRoundedRect(Shift(rect), radius, color, lineWidth);
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const Color& color, float lineWidth) override {
        inner_->StrokeLine(x0 + dx_, y0 + dy_, x1 + dx_, y1 + dy_, color,
                           lineWidth);
    }
    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                  float baselineY, const Color& color) override {
        inner_->DrawText(utf8, font, x + dx_, baselineY + dy_, color);
    }
    void DrawLayout(const TextLayout& layout, float x, float topY,
                    const Color& color) override {
        inner_->DrawLayout(layout, x + dx_, topY + dy_, color);
    }
    void DrawBitmap(BitmapHandle image, const Rect& rect) override {
        inner_->DrawBitmap(image, Shift(rect));
    }
    void FillEllipse(const Rect& rect, const Color& color) override {
        inner_->FillEllipse(Shift(rect), color);
    }
    void StrokeEllipse(const Rect& rect, const Color& color,
                       float lineWidth) override {
        inner_->StrokeEllipse(Shift(rect), color, lineWidth);
    }
    void FillPolygon(const std::vector<Point>& points,
                     const Color& color) override {
        inner_->FillPolygon(Shift(points), color);
    }
    void DrawChevron(const Rect& rect, bool expanded,
                     const Color& color) override {
        inner_->DrawChevron(Shift(rect), expanded, color);
    }
    void StrokePolygon(const std::vector<Point>& points, const Color& color,
                       float lineWidth, bool closed) override {
        inner_->StrokePolygon(Shift(points), color, lineWidth, closed);
    }
    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                      const Color& color) override {
        inner_->FillPieSlice(Shift(rect), startAngle, endAngle, color);
    }
    void PushClip(const Rect& rect) override {
        inner_->PushClip(Shift(rect));
    }
    void PopClip() override { inner_->PopClip(); }

    // The shift and the wrapped target. A painter that draws on its own
    // concrete device, like the Windows SVG scene, reads the shift out
    // of the wrapper chain and applies it itself.
    float dx() const { return dx_; }
    float dy() const { return dy_; }
    RenderTarget* inner() const { return inner_; }

private:
    Rect Shift(const Rect& rect) const {
        Rect out = rect;
        out.x += dx_;
        out.y += dy_;
        return out;
    }
    std::vector<Point> Shift(const std::vector<Point>& points) const {
        std::vector<Point> out;
        out.reserve(points.size());
        for (const Point& p : points) {
            out.push_back(Point{p.x + dx_, p.y + dy_});
        }
        return out;
    }

    RenderTarget* inner_;
    float dx_;
    float dy_;
};

}  // namespace platform
