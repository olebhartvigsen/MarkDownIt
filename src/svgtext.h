#pragma once

// SVG text extraction utilities.
// Portable C++17, no Windows dependencies, unit-testable in WSL.

#include <cstdint>
#include <string>
#include <vector>
#include "svgtypes.h"

namespace svg {

// Extracts all visible <text> element runs from an SVG string.
// Skips text inside <defs>. Handles <tspan> children.
// Never throws; returns fewer results on malformed input.
std::vector<TextRun> ExtractTextRuns(const std::string& xml);

// Removes <text>, <tspan>, and <foreignObject> elements from an SVG string
// so Direct2D only sees the shapes it can render.
std::string StripTextElements(const std::string& xml);

// Reads the root <svg> width/height, falling back to the viewBox extent.
// Returns false when neither is present or usable. Portable, so the size of
// an inline SVG image can be known before it is drawn.
bool ReadSize(const std::string& xml, float* w, float* h);

// Resolves CSS light-dark(light, dark) to the branch the current page
// shows, everywhere it appears in the document: a style attribute, a
// <style> block or a presentation attribute. Returns the input unchanged
// when there is nothing to resolve.
//
// Both ports call this once, on the raw SVG text, before their own
// reader sees it: Direct2D's parser knows no light-dark() and the mac
// reader would otherwise have to carry an appearance through every
// parse function. One string transform at the boundary keeps the rule in
// one place and keeps the readers unchanged.
std::string ResolveLightDark(const std::string& xml, bool dark);

// Re-colours a diagram for a dark page.
//
// Plenty of SVG diagrams (draw.io exports, for one) bake a white canvas
// into the file: a full-size white rect, white node fills and near-black
// strokes and text. Dropped on a dark page untouched, that is a glowing
// white card, which is exactly the "half-finished look" the night mode
// guide warns about. This rewrites the colours in the markup instead:
// lightness is flipped (hue and saturation are kept, so a blue box stays
// blue), a white or near-white fill becomes the paper, and a black or
// near-black stroke becomes the ink. `none`, `transparent`, `currentColor`
// and paint servers (`url(#...)`) are left alone, and `<image>` data URIs
// are never touched.
std::string AdaptForDark(const std::string& xml, uint32_t paperRgb,
                         uint32_t inkRgb);

// Both of the above, in the order they must run. This is what both
// renderers call on the raw document, so the readers themselves stay
// appearance-blind.
std::string PrepareSvgForAppearance(const std::string& xml, bool dark,
                                    uint32_t paperRgb, uint32_t inkRgb);

// Rewrites marker-start/marker-end references into explicit <g> groups.
// Direct2D supports neither <marker> nor the marker-* presentation
// attributes, so every arrowhead in a diagram was silently dropped. The
// rewrite is geometric only: the marker's own content is copied verbatim and
// placed with the transform the SVG spec prescribes (translate to the path
// end, rotate to the path direction, scale by stroke-width, shift by
// refX/refY). Returns the input unchanged when nothing references a marker.
std::string ExpandMarkers(const std::string& xml);

}  // namespace svg
