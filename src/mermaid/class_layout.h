#pragma once
// Class diagram layout: dagre port matching mermaid classRenderer-v3.
#include "class_parse.h"
#include "layout_internal.h"
#include <string>
#include <vector>

namespace mermaid {

// Geometric output for one class box. All coordinates are in the same
// absolute space as the golden oracle (translateGraph shifts everything
// so the min side sits at marginx/marginy = 8).
struct LaidOutClassNode {
    std::string id;
    std::string title;
    std::vector<std::string> annotations;  // inner text without << >>
    std::vector<std::string> members;
    std::vector<std::string> methods;
    double x = 0, y = 0;                   // center
    double w = 0, h = 0;                   // box size (even numbers)
    double label_x = 0, label_y = 0, label_w = 0;
    std::vector<double> member_x, member_y;  // local (box-relative) anchor
    std::vector<double> method_x, method_y;
    std::vector<double> divider_y;           // local divider y positions
    double annotation_y = 0, annotation_w = 0;
};

struct LaidOutClassEdge {
    std::string from, to;
    std::string start_marker;  // aggregation / extension / composition / dependency
    std::string end_marker;
    std::string pattern;       // solid / dashed / dotted
    std::string d;             // basis-spline path, absolute coords
    std::vector<Point> points; // decoded M/L/C endpoints
    std::string label;
    double label_x = 0, label_y = 0, label_w = 0, label_h = 0;
};

struct LaidOutClassTerminal {
    std::string kind;          // "label" or "terminal"
    std::string text;
    double x = 0, y = 0, w = 0, h = 0, ix = 0, iy = 0;
};

struct LaidOutClass {
    std::vector<LaidOutClassNode> nodes;
    std::vector<LaidOutClassEdge> edges;
    std::vector<LaidOutClassTerminal> edge_labels;
    double width = 0, height = 0, startx = 0, starty = 0;
    double vbwidth = 0, vbheight = 0;
    std::string error;
};

// Layout a parsed class diagram. Runs the same dagre pipeline the
// flowchart uses (MakeAcyclic..Denormalize) with class parameters:
// nodesep=ranksep=50 (ranksep halved internally by makeSpaceForEdgeLabels
// emulation), edgesep=20, margin 8, and edge-label proxies sized
// 8*len x 20 on the middle rank of labeled edges.
LaidOutClass LayoutClassDiagram(const ClassDiagram& diag);

}  // namespace mermaid