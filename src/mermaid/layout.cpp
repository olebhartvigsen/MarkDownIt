// Public mermaid layout wrapper. Chains the six phases and reports bbox.
#include "layout.h"
#include "layout_internal.h"
#include "parse.h"

#include <algorithm>
#include <limits>

namespace mermaid {

LayoutGraph FlowchartToLayoutGraph(const Flowchart& flow) {
    LayoutGraph g;
    g.nodes.reserve(flow.nodes.size());
    for (const auto& n : flow.nodes) {
        LayoutNode ln;
        ln.id = static_cast<int>(g.nodes.size());
        ln.label = n.label.empty() ? n.id : n.label;
        // Match dump.mjs oracle: CHAR_W=8.4, LINE_H=19, PADDING=15.
        const double CHAR_W = 8.4, LINE_H = 19.0, PADDING = 15.0;
        double len = static_cast<double>(ln.label.size());
        double raw_w = len * CHAR_W;
        if (raw_w < 14.0) raw_w = 14.0;
        ln.width  = static_cast<float>(raw_w + PADDING * 2.0);
        ln.height = static_cast<float>(LINE_H + PADDING * 2.0);
        g.nodes.push_back(ln);
    }
    g.edges.reserve(flow.edges.size());
    for (const auto& e : flow.edges) {
        LayoutEdge le;
        le.from = e.from;
        le.to = e.to;
        le.minlen = e.minlen > 0 ? e.minlen : 1;
        le.weight = e.weight > 0 ? e.weight : 1;
        le.label = e.label;
        g.edges.push_back(le);
    }
    return g;
}

LaidOutFlowchart LayoutFlowchart(const Flowchart& flow, const LayoutParams& p) {
    LaidOutFlowchart out;
    LayoutGraph g = FlowchartToLayoutGraph(flow);
    if (g.nodes.empty()) return out;

    MakeAcyclic(g);
    AssignRanks(g);
    Normalize(g);
    Order(g);
    AssignCoordinates(g, p);
    RouteEdges(g, p);
    Denormalize(g);

    // Shift by margin so nothing sits at (0,0) exactly and bbox includes it.
    double margin = p.margin;
    if (margin > 0.0) {
        for (auto& n : g.nodes) {
            n.x = static_cast<float>(n.x + margin);
            n.y = static_cast<float>(n.y + margin);
        }
        for (auto& e : g.edges) {
            for (auto& pt : e.route) { pt.x += margin; pt.y += margin; }
        }
    }

    // Bounding box: max of node right/bottom edges and edge route points.
    double max_x = 0.0, max_y = 0.0;
    for (const auto& n : g.nodes) {
        double right  = static_cast<double>(n.x) + static_cast<double>(n.width)  * 0.5;
        double bottom = static_cast<double>(n.y) + static_cast<double>(n.height) * 0.5;
        if (right  > max_x) max_x = right;
        if (bottom > max_y) max_y = bottom;
    }
    for (const auto& e : g.edges) {
        for (const auto& pt : e.route) {
            if (pt.x > max_x) max_x = pt.x;
            if (pt.y > max_y) max_y = pt.y;
        }
    }
    out.width  = max_x + margin;
    out.height = max_y + margin;
    out.nodes = std::move(g.nodes);
    out.edges = std::move(g.edges);
    return out;
}

}  // namespace mermaid
