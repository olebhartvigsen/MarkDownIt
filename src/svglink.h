#pragma once

// Links inside an SVG diagram.
//
// draw.io and its kin let an author put a URL on a shape, and the export
// writes it as an <a href="..."> around the shape's own elements. A
// diagram in a document is then a place where a link looks like a link
// and does nothing, which is the gap this header closes.
//
// The scan is deliberately geometry-light: it takes the union of the
// boxes of the children it understands (rect, ellipse, circle, line,
// polygon, polyline, path and text) and treats that union as the link's
// target. Both renderers draw the same elements, so the box a reader
// clicks is the box they see outlined by the shape.
//
// This lives in shared code because the answer must be the same on both
// ports: the Windows build draws through Direct2D and the mac build
// through its own reader, and neither of those exposes link geometry.

#include <string>
#include <vector>

namespace svg {

// One clickable region, in the SVG's own user units (the viewBox space).
struct LinkBox {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // The href as the document wrote it, entity-decoded.
    std::string href;
};

// Every <a> with an href and at least one shape inside it. Anchors with
// no geometry (a stray <a> around text only, for instance) still count:
// their text position gets a small box.
std::vector<LinkBox> CollectLinks(const std::string& xml);

// The link under a point in user units, or nullptr. Later boxes win,
// which matches the paint order: the shape drawn last is the one on top.
const LinkBox* LinkAt(const std::vector<LinkBox>& links, float u, float v);

// One drawn picture's link boxes, placed where the picture was drawn:
// the origin in the caller's own coordinate space and the scale from
// the SVG's user units to it. The mac renderer keeps a picture in view
// points and the Windows one in document points; the box list and the
// lookup are the same, so both send this struct.
struct LinkArea {
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;   // caller points per user unit
    std::vector<LinkBox> links;
};

// The link under a point in the caller's space, across every picture.
// Later pictures win, the paint order of the page.
const LinkBox* LinkInAreas(const std::vector<LinkArea>& areas, float x,
                           float y);

// Whether the app may hand this URL to the system browser. Only the
// schemes a document should be able to open: https and mailto. Plain
// http is refused along with javascript:, data:, file: and bare
// relative paths, because a diagram is untrusted input and an
// unencrypted target cannot be told from a tampered one.
bool IsOpenableUrl(const std::string& href);

}  // namespace svg
