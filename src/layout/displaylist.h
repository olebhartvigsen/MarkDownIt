// The display list of plan task 3.1: what the shared block layout
// emits and both renderers paint. One word about the shape: every item
// is a plain struct in the seam's own types, draw order is data in one
// vector, and nothing here holds a platform handle, so a dump prints
// the same bytes from either runner.
//
// Units and coordinates, binding on both runners:
// - One document point everywhere; zoom is baked in at build time
//   because the wrap widths need it. The painters translate once for
//   scroll and never rescale items.
// - Origin is the top left of the content area, x right, y down.
// - No rounding: a coordinate is computed once in shared layout and
//   handed to the platform untouched. Anything that snaps or aligns
//   lives below the RenderTarget, and cannot happen here.
// - Character positions are UTF-8 byte offsets into the drawn text,
//   the same unit the text seam's hit test contract uses.

#ifndef MARKDOWNIT_SRC_LAYOUT_DISPLAYLIST_H_
#define MARKDOWNIT_SRC_LAYOUT_DISPLAYLIST_H_

#include "platform/seams.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace display {

using platform::Color;
using platform::ImageKind;
using platform::Point;
using platform::Rect;
using platform::TextLine;

// Fill -----------------------------------------------------------------

// A flat rectangle: selection rows, the quote bar, table row washes,
// marker and find fills, the marker's bottom line.
struct FillRect {
    Rect rect;
    Color color;
};

// A rounded card: the fenced code block, the table header, the node
// and lane boxes the mermaid engine lays out.
struct FillRoundedRect {
    Rect rect;
    float radius = 0.0f;
    Color color;
};

// The inline code pill split over wrapped lines: the first fragment
// keeps its left cap, the last its right, middles stay square, so the
// fragments read as one run.
struct FillRoundedRectSides {
    Rect rect;
    float radius = 0.0f;
    Color color;
    bool roundLeft = false;
    bool roundRight = false;
};

// Stroke ---------------------------------------------------------------

// A stroked box: the task checkbox, the table cell frame.
struct StrokeRect {
    Rect rect;
    Color color;
    float lineWidth = 1.0f;
};

struct StrokeRoundedRect {
    Rect rect;
    float radius = 0.0f;
    Color color;
    float lineWidth = 1.0f;
};

// A straight segment: the thematic break, the heading rule, an
// underline or a strike of a run, table grid lines, a lifeline.
struct StrokeLine {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    Color color;
    float lineWidth = 1.0f;
    // Dash and gap lengths in points, empty for a solid line. The
    // mermaid LineStyle values are resolved into these numbers once,
    // here in shared layout, so both painters stroke the same pen
    // instead of each reading the style its own way.
    std::vector<float> dash;
};

// A polyline through laid out points: the mermaid edge route (open),
// an SVG path, the outline of a polygon node.
struct StrokePolygon {
    // The route, like LayoutEdge::route: at least two points.
    std::vector<Point> points;
    Color color;
    float lineWidth = 1.0f;
    bool closed = false;
    std::vector<float> dash;  // empty for a solid line
};

// Shape and pie --------------------------------------------------------

struct FillEllipse {
    Rect rect;  // the ellipse inscribed in the rectangle
    Color color;
};

struct StrokeEllipse {
    Rect rect;
    Color color;
    float lineWidth = 1.0f;
};

// A filled polygon: an arrowhead, a diamond node, a list figurine. The
// last point joins back to the first; the points keep their order.
struct FillPolygon {
    std::vector<Point> points;
    Color color;
};

// One pie slice, in the seam's terms: the bounds hold the whole pie,
// the angles are degrees clockwise from the three o'clock position.
// The mermaid PieArc radians, which count from twelve o'clock, are
// converted into this form exactly once, in shared layout.
struct PieSlice {
    Rect bounds;
    float startAngle = 0.0f;
    float endAngle = 0.0f;
    Color color;
};

// Text -----------------------------------------------------------------

// One laid out line of text at its place in the document. The runs
// carry their own fonts, colors, run offsets and effects, and a run's
// baseline is top + line.ascent. The line is the engine's output
// normalized to the theme's line heights before recording, so the
// line height arithmetic lives in shared layout, not in a painter.
//
// srcOffset and srcLength name the source bytes this line draws, so a
// caret, a find match and a mark anchor without re walking the DOM.
// The per byte map a wrapped line needs arrives with plan task 3.3.
struct Text {
    float x = 0.0f;    // the left edge of the line
    float top = 0.0f;  // the top of the line's box
    TextLine line;
    uint32_t srcOffset = 0;  // first source byte this line draws
    uint32_t srcLength = 0;  // how many source bytes it draws
};

// Image ----------------------------------------------------------------

// An image at its place and size. url is the cache identity: a path or
// URL for a raster picture, the fence contents for an SVG block. The
// painter resolves the identity against its own cache; the item stays
// handle free, so a dump prints the same bytes from either platform.
struct Image {
    std::string url;
    ImageKind kind = ImageKind::Raster;
    Rect rect;  // where it is drawn, already fitted
    float naturalWidth = 0.0f;   // intrinsic size, for fitting
    float naturalHeight = 0.0f;
};

// Clip -----------------------------------------------------------------

// Clips stack: each PushClip matches a PopClip later in the item
// order, the way the seam's PushClip and PopClip pair.
struct PushClip {
    Rect rect;
};

struct PopClip {};

// The list -------------------------------------------------------------

// One drawable thing, in the paint order it was recorded. The order is
// the z order: washes beneath, text above, the list decides, and the
// painters replay it without reseating.
using Item = std::variant<FillRect, FillRoundedRect, FillRoundedRectSides,
                          StrokeRect, StrokeRoundedRect, StrokeLine,
                          StrokePolygon, FillEllipse, StrokeEllipse,
                          FillPolygon, PieSlice, Text, Image, PushClip,
                          PopClip>;

// The whole document. The painters translate once for scroll and
// replay the items in order; width and height are the widest and
// tallest drawn edges, the scroll and content range.
struct DisplayList {
    std::vector<Item> items;
    float width = 0.0f;
    float height = 0.0f;
};

}  // namespace display

#endif  // MARKDOWNIT_SRC_LAYOUT_DISPLAYLIST_H_
