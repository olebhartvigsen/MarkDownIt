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

// Whether the app may hand this URL to the system browser. Only the
// schemes a document should be able to open: http, https and mailto.
// Everything else (javascript:, data:, file:, a bare relative path) is
// refused, because a diagram is untrusted input.
bool IsOpenableUrl(const std::string& href);

}  // namespace svg
