#pragma once

// The Core Graphics render target.
//
// The target draws into a CGContext through the RenderTarget seam, so
// the drawing code never touches a Core Graphics call directly. It
// lives in a header of its own because the content view and the outline
// pane both draw through it.
//
// The context it draws into is flipped: the origin is at the top left
// and the y axis points down, which is the space the renderer lays the
// document out in.

#import <CoreGraphics/CoreGraphics.h>

#include "platform/seams.h"
#include "image_draw_mac.h"
#include "text_engine_mac.h"

namespace mac {

// The value of pi, for the arcs.
constexpr double kPi = 3.14159265358979323846;

class CoreGraphicsTarget : public RenderTarget {
public:
    explicit CoreGraphicsTarget(CGContextRef context) : context_(context) {}

    void FillRect(const Rect& rect, const Color& color) override {
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                               rect.height));
        CGContextRestoreGState(context_);
    }

    void FillRoundedRect(const Rect& rect, float radius,
                         const Color& color) override {
        if (radius <= 0.0f) {
            FillRect(rect, color);
            return;
        }
        const CGRect bounds = CGRectMake(rect.x, rect.y, rect.width, rect.height);
        // The radius may not overrun the shape, so a thin band keeps
        // sensible ends.
        const CGFloat r =
            std::min<CGFloat>(radius, std::min(bounds.size.width,
                                               bounds.size.height) * 0.5f);

        // A single rounded path composites a translucent fill once.
        // Drawing a middle strip plus four corner ellipses in the same
        // colour overlaps at the corners and leaves darker dots there.
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGMutablePathRef path = CGPathCreateMutable();
        if (path) {
            CGPathAddRoundedRect(path, nullptr, bounds, r, r);
            CGContextAddPath(context_, path);
            CGContextFillPath(context_);
            CGPathRelease(path);
        }
        CGContextRestoreGState(context_);
    }

    void FillRoundedRectSides(const Rect& rect, float radius,
                              const Color& color, bool roundLeft,
                              bool roundRight) override {
        if (radius <= 0.0f || (!roundLeft && !roundRight)) {
            FillRect(rect, color);
            return;
        }
        if (roundLeft && roundRight) {
            FillRoundedRect(rect, radius, color);
            return;
        }

        const CGRect bounds = CGRectMake(rect.x, rect.y, rect.width,
                                         rect.height);
        const CGFloat r =
            std::min<CGFloat>(radius, std::min(bounds.size.width,
                                               bounds.size.height) * 0.5f);
        const CGFloat left = CGRectGetMinX(bounds);
        const CGFloat right = CGRectGetMaxX(bounds);
        const CGFloat top = CGRectGetMinY(bounds);
        const CGFloat bottom = CGRectGetMaxY(bounds);
        const CGFloat half = static_cast<CGFloat>(kPi) * 0.5f;
        const CGFloat full = static_cast<CGFloat>(kPi);

        // One path, filled once: overlapping strips and corner
        // circles darken a translucent fill at the seams.
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGMutablePathRef path = CGPathCreateMutable();
        if (path) {
            CGPathMoveToPoint(path, nullptr, left + (roundLeft ? r : 0.0f),
                              top);
            CGPathAddLineToPoint(path, nullptr,
                                 right - (roundRight ? r : 0.0f), top);
            if (roundRight) {
                CGPathAddArc(path, nullptr, right - r, top + r, r, -half,
                             0.0f, false);
                CGPathAddLineToPoint(path, nullptr, right, bottom - r);
                CGPathAddArc(path, nullptr, right - r, bottom - r, r, 0.0f,
                             half, false);
            } else {
                CGPathAddLineToPoint(path, nullptr, right, top);
                CGPathAddLineToPoint(path, nullptr, right, bottom);
            }
            CGPathAddLineToPoint(path, nullptr,
                                 left + (roundLeft ? r : 0.0f), bottom);
            if (roundLeft) {
                CGPathAddArc(path, nullptr, left + r, bottom - r, r, half,
                             full, false);
                CGPathAddLineToPoint(path, nullptr, left, top + r);
                CGPathAddArc(path, nullptr, left + r, top + r, r, full,
                             full + half, false);
            } else {
                CGPathAddLineToPoint(path, nullptr, left, bottom);
                CGPathAddLineToPoint(path, nullptr, left, top);
            }
            CGPathCloseSubpath(path);
            CGContextAddPath(context_, path);
            CGContextFillPath(context_);
            CGPathRelease(path);
        }
        CGContextRestoreGState(context_);
    }

    void StrokeRect(const Rect& rect, const Color& color,
                    float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextStrokeRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                                 rect.height));
        CGContextRestoreGState(context_);
    }

    void StrokeRoundedRect(const Rect& rect, float radius,
                           const Color& color, float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b,
                                   color.a);
        CGContextSetLineWidth(context_, lineWidth > 0.0f ? lineWidth : 1.0f);
        const CGRect bounds = CGRectMake(rect.x, rect.y, rect.width,
                                         rect.height);
        const CGFloat cornerRadius = radius > 0.0f ? radius : 0.0f;
        CGMutablePathRef path = CGPathCreateMutable();
        if (path) {
            CGPathAddRoundedRect(path, nullptr, bounds, cornerRadius,
                                 cornerRadius);
            CGContextAddPath(context_, path);
            CGContextStrokePath(context_);
            CGPathRelease(path);
        }
        CGContextRestoreGState(context_);
    }

    void StrokeLine(float x0, float y0, float x1, float y1, const Color& color,
                    float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextMoveToPoint(context_, x0, y0);
        CGContextAddLineToPoint(context_, x1, y1);
        CGContextStrokePath(context_);
        CGContextRestoreGState(context_);
    }

    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                  float baselineY, const Color& color) override {
        if (utf8.empty()) return;

        // The text as one line, drawn with Core Text so the drawing side
        // takes exactly the font the measuring side took.
        CFStringRef text = CFStringCreateWithBytes(
            kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.data()),
            static_cast<CFIndex>(utf8.size()), kCFStringEncodingUTF8, false);
        if (!text) return;

        CTFontRef ctFont = platform::CoreTextFontFor(font);
        if (!ctFont) {
            CFRelease(text);
            return;
        }

        CGColorRef cgColor = CGColorCreateGenericRGB(color.r, color.g,
                                                     color.b, color.a);
        if (!cgColor) {
            CFRelease(text);
            return;
        }

        const void* keys[] = {kCTFontAttributeName,
                              kCTForegroundColorAttributeName};
        const void* values[] = {ctFont, cgColor};
        CFDictionaryRef attributes = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 2,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFRelease(cgColor);
        if (!attributes) {
            CFRelease(text);
            return;
        }

        CFAttributedStringRef attributed =
            CFAttributedStringCreate(kCFAllocatorDefault, text, attributes);
        CFRelease(attributes);
        CFRelease(text);
        if (!attributed) return;

        CTLineRef line = CTLineCreateWithAttributedString(attributed);
        CFRelease(attributed);
        if (!line) return;

        CGContextSaveGState(context_);
        // The context is flipped, with the y axis pointing downwards, so
        // the text matrix is flipped back. That is what stands the glyphs
        // up and what puts the baseline on the y the renderer asked for.
        CGContextSetTextMatrix(context_, CGAffineTransformMakeScale(1.0, -1.0));
        CGContextSetTextPosition(context_, x, baselineY);
        CTLineDraw(line, context_);
        CGContextRestoreGState(context_);

        CFRelease(line);
    }

    void DrawBitmap(BitmapHandle image, const Rect& rect) override {
        platform::DrawImageIntoContext(context_, image, rect);
    }

    void DrawChevron(const Rect& rect, bool expanded,
                     const Color& color) override {
        // The system's own chevron symbol, and the small triangle as
        // the fallback when the symbol is not there.
        if (!platform::DrawChevronIntoContext(context_, rect, expanded, color)) {
            RenderTarget::DrawChevron(rect, expanded, color);
        }
    }

    void FillEllipse(const Rect& rect, const Color& color) override {
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillEllipseInRect(context_,
                                   CGRectMake(rect.x, rect.y, rect.width,
                                              rect.height));
        CGContextRestoreGState(context_);
    }

    void StrokeEllipse(const Rect& rect, const Color& color,
                       float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextStrokeEllipseInRect(context_,
                                     CGRectMake(rect.x, rect.y, rect.width,
                                                rect.height));
        CGContextRestoreGState(context_);
    }

    void FillPolygon(const std::vector<Point>& points,
                     const Color& color) override {
        if (points.size() < 3) return;
        // Core Graphics takes the points as one array.
        std::vector<CGPoint> coordinates;
        coordinates.reserve(points.size());
        for (const Point& point : points) {
            coordinates.push_back(CGPointMake(point.x, point.y));
        }
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, coordinates[0].x, coordinates[0].y);
        CGContextAddLines(context_, coordinates.data(), coordinates.size());
        CGContextClosePath(context_);
        CGContextFillPath(context_);
        CGContextRestoreGState(context_);
    }

    void StrokePolygon(const std::vector<Point>& points,
                       const Color& color, float lineWidth,
                       bool closed) override {
        if (points.size() < 2) return;
        std::vector<CGPoint> coordinates;
        coordinates.reserve(points.size());
        for (const Point& point : points) {
            coordinates.push_back(CGPointMake(point.x, point.y));
        }
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b,
                                   color.a);
        CGContextSetLineWidth(context_, lineWidth > 0.0f ? lineWidth : 1.0f);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, coordinates[0].x, coordinates[0].y);
        CGContextAddLines(context_, coordinates.data(), coordinates.size());
        if (closed) CGContextClosePath(context_);
        CGContextStrokePath(context_);
        CGContextRestoreGState(context_);
    }

    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                      const Color& color) override {
        // The seam counts the angles clockwise from three o'clock, and
        // Core Graphics counts them counter-clockwise, so the angles are
        // negated. The context is flipped, with the y axis pointing
        // downwards, so the negated angle sweeps clockwise on screen.
        const CGFloat centreX = rect.x + rect.width * 0.5;
        const CGFloat centreY = rect.y + rect.height * 0.5;
        const CGFloat radius = std::min(rect.width, rect.height) * 0.5;
        const CGFloat start = -startAngle * kPi / 180.0;
        const CGFloat end = -endAngle * kPi / 180.0;
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, centreX, centreY);
        CGContextAddArc(context_, centreX, centreY, radius, start, end, true);
        CGContextClosePath(context_);
        CGContextFillPath(context_);
        CGContextRestoreGState(context_);
    }

    void PushClip(const Rect& rect) override {
        CGContextSaveGState(context_);
        CGContextClipToRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                                 rect.height));
    }

    void PopClip() override { CGContextRestoreGState(context_); }

private:
    CGContextRef context_;
};

}  // namespace mac
