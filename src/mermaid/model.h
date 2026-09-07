#pragma once
// Portable flowchart model. No Windows, no Direct2D: unit tested on any host.
#include <string>
#include <vector>
#include "layout_internal.h"

namespace mermaid {

enum class Dir { TB, BT, LR, RL };
enum class Shape { Rect, Round, Stadium, Diamond, Circle };
// LineStyle/Head now live in layout_internal.h (shared with LayoutEdge).

struct FlowNode {
    std::string id;
    std::string label;
    Shape shape = Shape::Rect;
};

struct FlowEdge {
    int from = -1;
    int to = -1;
    std::string label;
    LineStyle style = LineStyle::Solid;
    Head head = Head::Arrow;
    int minlen = 1;
    int weight = 1;
};

// Swimlane grouping. Post-layout we compute a bounding band around the
// contained node indices. Nested subgraphs are supported in the model,
// though the current lane-box computation ignores nesting (lanes are
// flat rectangles). Optional direction override is parsed but not yet
// used by the layout pipeline; see swimlanes.cpp.
struct Subgraph {
    std::string id;
    std::string title;
    Dir direction = Dir::TB;
    bool has_direction = false;
    std::vector<int> node_indices;
    std::vector<int> child_subgraphs;
};

struct Flowchart {
    Dir dir = Dir::TB;
    std::vector<FlowNode> nodes;
    std::vector<FlowEdge> edges;
    std::vector<Subgraph> subgraphs;
    std::string error;
};

struct LaneBox {
    std::string id;
    std::string title;
    float x = 0, y = 0, width = 0, height = 0;
};

struct LaidOutFlowchart {
    std::vector<LayoutNode> nodes;
    std::vector<LayoutEdge> edges;
    std::vector<LaneBox> lanes;
    double width = 0.0;
    double height = 0.0;
};

}  // namespace mermaid
