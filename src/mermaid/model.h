#pragma once
// Portable flowchart model. No Windows, no Direct2D: unit tested on any host.
#include <string>
#include <vector>
#include "layout_internal.h"

namespace mermaid {

enum class Dir { TB, BT, LR, RL };
enum class Shape { Rect, Round, Stadium, Diamond, Circle };
enum class LineStyle { Solid, Dotted, Thick };
enum class Head { None, Arrow };

struct FlowNode {
    std::string id;
    std::string label;
    Shape shape = Shape::Rect;
};

struct FlowEdge {
    int from = -1;          // index into Flowchart::nodes
    int to = -1;
    std::string label;
    LineStyle style = LineStyle::Solid;
    Head head = Head::Arrow;
    int minlen = 1;
    int weight = 1;
};

struct Flowchart {
    Dir dir = Dir::TB;
    std::vector<FlowNode> nodes;
    std::vector<FlowEdge> edges;
    std::string error;      // non-empty => parse failed, caller falls back to code block
};

struct LaidOutFlowchart {
    std::vector<LayoutNode> nodes;
    std::vector<LayoutEdge> edges;
    double width = 0.0;
    double height = 0.0;
};

}  // namespace mermaid
