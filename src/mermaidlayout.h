#pragma once

// Turns a parsed mermaid::Diagram into positioned geometry.
// Portable C++17: no Windows types, so it is unit testable in WSL.

#include "mermaid.h"
#include <string>
#include <vector>
#include <utility>

namespace mermaid {

struct LaidOutNode {
    float x = 0, y = 0, w = 0, h = 0;   // top-left plus size, in DIPs
    std::string label;
    NodeShape shape = NodeShape::Rect;
};

struct LaidOutEdge {
    std::vector<std::pair<float, float>> points;  // polyline, >= 2 points
    std::string label;
    float labelX = 0, labelY = 0;
    EdgeStyle style = EdgeStyle::Solid;
    ArrowHead head  = ArrowHead::Arrow;
};

struct Layout {
    std::vector<LaidOutNode> nodes;
    std::vector<LaidOutEdge> edges;
    float width = 0, height = 0;
};

// measureText returns the width in DIPs of a label at the diagram font size.
// The renderer passes a DirectWrite-backed callback; tests pass a stub.
using MeasureFn = float (*)(const std::string& text, void* ctx);

Layout ComputeLayout(const Diagram& d, float maxWidth,
                     MeasureFn measure, void* measureCtx);

}  // namespace mermaid
