#pragma once
// State diagram layout (flat states): dagre port matching mermaid 11.x
// state renderer. Mirrors the class layout pipeline with state node sizes.
#include "state_parse.h"
#include "layout_internal.h"
#include <string>
#include <vector>

namespace mermaid {

struct LaidOutStateNode {
    std::string id;
    std::string kind;      // start / end / state
    double cx = 0, cy = 0; // dagre center (+margin, final coords)
    double w = 0, h = 0;   // rendered bbox (start/end: circle r*2; state: rect)
    double r = 0;          // circle radius for start/end
    std::string text;
};

struct LaidOutStateEdge {
    std::string from, to;
    std::string d;             // basis-spline path, absolute coords
    std::vector<Point> points; // decoded M/L/C endpoints
};

struct LaidOutStateLabel {
    std::string text;
    double x = 0, y = 0, w = 0, h = 0;
};

struct LaidOutState {
    std::vector<LaidOutStateNode> nodes;
    std::vector<LaidOutStateEdge> edges;
    std::vector<LaidOutStateLabel> labels;
    double width = 0, height = 0, startx = 0, starty = 0;
    double vbwidth = 0, vbheight = 0;
    std::string error;
};

// Layout a parsed state diagram (flat states only). Composite states and
// self transitions are rejected with error set.
LaidOutState LayoutStateDiagram(const StateDiagram& diag);

}  // namespace mermaid