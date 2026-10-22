#pragma once

// The Core Graphics render target.
//
// The target draws into a CGContext through the RenderTarget seam, so
// the drawing code never touches a Core Graphics call directly. It
// lives in a header of its own because the content view and the outline
// pane both draw through it.

#import <CoreGraphics/CoreGraphics.h>

#include "seams.h"
#include "image_draw_mac.h"

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
        const CGRect box = CGRectInset(bounds, radius, radius);

        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextAddRect(context_, box);
        CGContextClip(context_);
        CGContextFillEllipseInRect(context_,
                                   CGRectMake(box.origin.x, box.origin.y,
                                              box.size.width, box.size.height));
        CGContextRestoreGState(context_);

        // The four corners.
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillEllipseInRect(
            context_, CGRectMake(bounds.origin.x, bounds.origin.y, radius * 2.0f,
                                 radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x + bounds.size.width - radius * 2.0f,
                       bounds.origin.y, radius * 2.0f, radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x,
                       bounds.origin.y + bounds.size.height - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x + bounds.size.width - radius * 2.0f,
                       bounds.origin.y + bounds.size.height - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f));
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
        NSString* text = [[NSString alloc] initWithBytes:utf8.data()
                                                   length:utf8.size()
                                                 encoding:NSUTF8StringEncoding];
        if (!text) return;

        NSFont* nsFont = BuildFont(font);
        if (!nsFont) return;

        NSDictionary* attributes = @{
            NSFontAttributeName : nsFont,
            NSForegroundColorAttributeName :
                [NSColor colorWithSRGBRed:color.r green:color.g blue:color.b
                                     alpha:color.a]
        };

        [text drawAtPoint:NSMakePoint(x, baselineY) withAttributes:attributes];
    }

    void DrawBitmap(BitmapHandle image, const Rect& rect) override {
        mac::DrawImageIntoContext(context_, image, rect);
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
    // Builds an NSFont from a font request.
    static NSFont* BuildFont(const FontSpec& spec) {
        NSFontWeight weight = spec.bold ? NSFontWeightBold : NSFontWeightRegular;
        NSFont* font = nil;

        if (spec.monospace) {
            const char* families[] = {"Menlo", "SF Mono", "Monaco",
                                      "Courier New"};
            for (const char* family : families) {
                font = [NSFont fontWithName:[NSString stringWithUTF8String:family]
                                      size:spec.size];
                if (font) break;
            }
        } else {
            const char* families[] = {"Helvetica Neue", "Helvetica", "SF Pro Text",
                                      "Lucida Grande"};
            for (const char* family : families) {
                font = [NSFont fontWithName:[NSString stringWithUTF8String:family]
                                      size:spec.size];
                if (font) break;
            }
        }

        if (!font) {
            font = [NSFont systemFontOfSize:spec.size weight:weight];
        }

        if (spec.italic) {
            // There is no class method that takes an italic flag, so the
            // italic is asked for through a font descriptor.
            NSFontDescriptor* descriptor =
                [font.fontDescriptor fontDescriptorWithSymbolicTraits:
                                       NSFontDescriptorTraitItalic];
            NSFont* italic = [NSFont fontWithDescriptor:descriptor size:spec.size];
            if (italic) font = italic;
        }
        return font;
    }

    CGContextRef context_;
};

}  // namespace mac
