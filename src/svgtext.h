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

}  // namespace svg
