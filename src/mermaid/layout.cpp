// Public mermaid layout wrapper. Chains the six phases and reports bbox.
#include "layout.h"
#include "layout_internal.h"
#include "parse.h"

#include <algorithm>
#include <limits>
#include <utility>

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
        switch (n.shape) {
            case Shape::Rect:    ln.shape = NodeShape::Rect; break;
            case Shape::Round:   ln.shape = NodeShape::Round; break;
            case Shape::Stadium: ln.shape = NodeShape::Stadium; break;
            case Shape::Diamond: ln.shape = NodeShape::Diamond; break;
            case Shape::Circle:  ln.shape = NodeShape::Circle; break;
        }
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

// Apply rankdir post-layout transform. Layout phases produce TB-relative
// coordinates (x grows right, y grows down). Swap into the requested
// direction before the final margin/bbox pass.
static void ApplyRankdir(LayoutGraph& g, Dir dir) {
    if (dir == Dir::TB) return;

    // Compute pre-transform bbox around node halves and route points.
    double max_x = 0.0, max_y = 0.0;
    for (const auto& n : g.nodes) {
        double r = static_cast<double>(n.x) + static_cast<double>(n.width)  * 0.5;
        double b = static_cast<double>(n.y) + static_cast<double>(n.height) * 0.5;
        if (r > max_x) max_x = r;
        if (b > max_y) max_y = b;
    }
    for (const auto& e : g.edges) {
        for (const auto& p : e.route) {
            if (p.x > max_x) max_x = p.x;
            if (p.y > max_y) max_y = p.y;
        }
    }

    if (dir == Dir::BT) {
        for (auto& n : g.nodes) n.y = static_cast<float>(max_y - n.y);
        for (auto& e : g.edges)
            for (auto& p : e.route) p.y = max_y - p.y;
        return;
    }

    // LR / RL: swap axes.
    for (auto& n : g.nodes) {
        std::swap(n.x, n.y);
        std::swap(n.width, n.height);
    }
    for (auto& e : g.edges) {
        for (auto& p : e.route) std::swap(p.x, p.y);
    }

    if (dir == Dir::RL) {
        // Mirror x across new width (which was the old max_y).
        double new_w = max_y;
        for (auto& n : g.nodes) n.x = static_cast<float>(new_w - n.x);
        for (auto& e : g.edges)
            for (auto& p : e.route) p.x = new_w - p.x;
    }
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

    ApplyRankdir(g, flow.dir);

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
