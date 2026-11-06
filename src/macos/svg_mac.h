#pragma once

// An SVG document for the macOS renderer.
//
// The Windows build draws an SVG through Direct2D, which reads the
// file as a document of its own. macOS has no such reader, so the
// elements the app handles are parsed here and drawn with the render
// target.
//
// The parser covers the shapes a markdown file holds in practice: the
// rectangles, the ellipses, the lines, the paths, the groups with a
// transform, and the text.

#include <string>
#include <vector>

#include "seams.h"

namespace mac {

class SvgDoc {
public:
    // What a shape is.
    enum class Kind {
        Rect,
        Ellipse,
        Line,
        Polygon,
        Text,
    };

    // The style a shape is drawn with.
    struct Style {
        Color fill;
        bool hasFill = false;
        Color stroke;
        bool hasStroke = false;
        float strokeWidth = 1.0f;
        float fontSize = 12.0f;
        std::string fontFamily;
        bool bold = false;
    };

    // The placement of a shape.
    struct Placement {
        float translateX = 0.0f;
        float translateY = 0.0f;
        float scale = 1.0f;
    };

    // One shape of the document.
    struct Shape {
        Kind kind = Kind::Rect;
        // The points of the shape, in the coordinates of the document.
        std::vector<Point> points;
        // The radius of an ellipse.
        float radiusX = 0.0f;
        float radiusY = 0.0f;
        // The text of a text shape.
        std::string text;
        // How the shape is drawn.
        Style style;
        // Where the shape sits.
        Placement placement;
    };

    // Parses the document. Returns false when it cannot be read.
    bool Load(const std::string& xml);

    // The natural size of the document, in points.
    float Width() const { return width_; }
    float Height() const { return height_; }

    // Draws the document into the rectangle, keeping the shape of it.
    void Draw(RenderTarget* target, float x, float y, float w,
              float h) const;

    // The shapes of the document, which the reader fills.
    std::vector<Shape>& shapes() { return shapes_; }

private:
    // The size of the document.
    float width_ = 0.0f;
    float height_ = 0.0f;
    // The view box, which says which part of the document is shown.
    float viewBoxX_ = 0.0f;
    float viewBoxY_ = 0.0f;
    float viewBoxWidth_ = 0.0f;
    float viewBoxHeight_ = 0.0f;
    // The shapes of the document.
    std::vector<Shape> shapes_;
};

}  // namespace mac
