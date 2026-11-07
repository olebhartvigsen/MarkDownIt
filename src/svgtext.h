#pragma once

// SVG text extraction utilities.
// Portable C++17, no Windows dependencies, unit-testable in WSL.

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

// Rewrites marker-start/marker-end references into explicit <g> groups.
// Direct2D supports neither <marker> nor the marker-* presentation
// attributes, so every arrowhead in a diagram was silently dropped. The
// rewrite is geometric only: the marker's own content is copied verbatim and
// placed with the transform the SVG spec prescribes (translate to the path
// end, rotate to the path direction, scale by stroke-width, shift by
// refX/refY). Returns the input unchanged when nothing references a marker.
std::string ExpandMarkers(const std::string& xml);

}  // namespace svg
