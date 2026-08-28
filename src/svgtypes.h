#pragma once

// Shared types for SVG text extraction and rendering.
// No Windows headers here, so svgtext.h/.cpp can be unit-tested in WSL.

#include <string>

namespace svg {

// A single text element extracted from an SVG document.
struct TextRun {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float fontSize = 16.0f;
    std::string fontFamily;
    std::string anchor;      // "start", "middle", "end"
    std::string fill;        // color as "#rrggbb" or name
    bool bold = false;
};

}  // namespace svg
