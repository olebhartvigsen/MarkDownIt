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

#include "platform/seams.h"

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
        // font-style="italic"/"oblique". Diagram annotations and the
        // <<stereotype>> labels are italic, and drawing them upright is a
        // visible difference from a browser.
        bool italic = false;
        // text-anchor="start"/"middle"/"end". Almost every label in a
        // diagram is centred on its box, so ignoring this shifted every
        // label right by half its width.
        std::string anchor;
        // stroke-dasharray, in user units. A diagram marks a planned or
        // missing link with a dashed line, and drawing it solid loses that
        // meaning.
        std::vector<float> dash;
    };

    // An affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f.
    //
    // A transform list can hold translate, scale, rotate, skew and matrix in
    // any order, so a placement is a matrix and not a translate and a scale.
    // The rotation used to be dropped, which pointed every arrowhead in a
    // diagram the same way, because a marker is placed with rotate in the
    // list.
    struct Placement {
        float a = 1.0f;
        float b = 0.0f;
        float c = 0.0f;
        float d = 1.0f;
        float e = 0.0f;
        float f = 0.0f;
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
        // True when the path ended with Z. An open path must not be stroked
        // closed, or a two-segment arrow grows a diagonal back to its start.
        bool closed = false;
    };

    // Parses the document. Returns false when it cannot be read.
    bool Load(const std::string& xml);

    // The natural size of the document, in points.
    float Width() const { return width_; }
    float Height() const { return height_; }

    // Draws the document into the rectangle, keeping the shape of it. The
    // engine measures a label so text-anchor can place it; without one the
    // text is drawn from its left edge.
    void Draw(RenderTarget* target, TextEngine* engine, float x, float y,
              float w, float h) const;

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
