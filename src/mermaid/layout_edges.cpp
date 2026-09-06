// Layout phase 6: edge routing (Task 10).
// Produces a polyline for each ORIGINAL edge: start on source-node border,
// dummy centers as bends, end on target-node border. Runs BEFORE Denormalize
// while dummy chains still exist.
#include "layout_internal.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace mermaid {

namespace {

// Intersect the ray from (cx,cy) toward (tx,ty) with an axis-aligned rectangle
// centered at (cx,cy) of size w x h. Returns the point on the border. If the
// two points coincide the center is returned.
Point ClipToBorder(double cx, double cy, double w, double h,
                   double tx, double ty) {
    double dx = tx - cx;
    double dy = ty - cy;
    if (dx == 0.0 && dy == 0.0) return Point{cx, cy};
    double inf = std::numeric_limits<double>::infinity();
    double tx_ = (dx != 0.0) ? (w * 0.5) / std::fabs(dx) : inf;
    double ty_ = (dy != 0.0) ? (h * 0.5) / std::fabs(dy) : inf;
    double t = tx_ < ty_ ? tx_ : ty_;
    return Point{cx + dx * t, cy + dy * t};
}

}  // namespace

void RouteEdges(LayoutGraph& g, const LayoutParams& /*p*/) {
    if (g.original_edges.empty()) {
        // Not normalized: route directly on g.edges as border-to-border segments.
        for (auto& e : g.edges) {
            const LayoutNode& u = g.nodes[static_cast<size_t>(e.from)];
            const LayoutNode& v = g.nodes[static_cast<size_t>(e.to)];
            Point p0 = ClipToBorder(u.x, u.y, u.width, u.height, v.x, v.y);
            Point p1 = ClipToBorder(v.x, v.y, v.width, v.height, u.x, u.y);
            e.route.clear();
            e.route.push_back(p0);
            e.route.push_back(p1);
        }
        return;
    }

    // Index dummy chains by original_edge_index.
    std::vector<const DummyChain*> chain_by_edge(g.original_edges.size(), nullptr);
    for (const auto& c : g.dummy_chains) {
        if (c.original_edge_index >= 0 &&
            static_cast<size_t>(c.original_edge_index) < chain_by_edge.size()) {
            chain_by_edge[static_cast<size_t>(c.original_edge_index)] = &c;
        }
    }

    for (size_t i = 0; i < g.original_edges.size(); ++i) {
        LayoutEdge& oe = g.original_edges[i];
        const LayoutNode& u = g.nodes[static_cast<size_t>(oe.from)];
        const LayoutNode& v = g.nodes[static_cast<size_t>(oe.to)];
        oe.route.clear();

        const DummyChain* chain = chain_by_edge[i];
        if (chain == nullptr || chain->dummy_nodes.empty()) {
            // Simple unit-span edge: border to border.
            Point p0 = ClipToBorder(u.x, u.y, u.width, u.height, v.x, v.y);
            Point p1 = ClipToBorder(v.x, v.y, v.width, v.height, u.x, u.y);
            oe.route.push_back(p0);
            oe.route.push_back(p1);
            continue;
        }

        // Long edge: source border -> each dummy center -> target border.
        const LayoutNode& first_d = g.nodes[static_cast<size_t>(chain->dummy_nodes.front())];
        Point src = ClipToBorder(u.x, u.y, u.width, u.height, first_d.x, first_d.y);
        oe.route.push_back(src);
        for (int did : chain->dummy_nodes) {
            const LayoutNode& d = g.nodes[static_cast<size_t>(did)];
            oe.route.push_back(Point{d.x, d.y});
        }
        const LayoutNode& last_d = g.nodes[static_cast<size_t>(chain->dummy_nodes.back())];
        Point dst = ClipToBorder(v.x, v.y, v.width, v.height, last_d.x, last_d.y);
        oe.route.push_back(dst);
    }
}

}  // namespace mermaid
