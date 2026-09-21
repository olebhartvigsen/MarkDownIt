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
    // dominant-baseline="central"/"middle": y is the vertical CENTER
    // of the glyphs, not the baseline (mermaid-exported SVGs).
    bool central = false;
    // Enclosing data-<g> bounding box of all <rect> children, in
    // document coordinates. Diagram exporters (Batik/Archi) position
    // label text relative to their shape box; label placement can
    // fall back to this box when no explicit text-anchor exists.
    bool boxValid = false;
    float bx = 0.0f;
    float by = 0.0f;
    float bw = 0.0f;
    float bh = 0.0f;
};

}  // namespace svg
