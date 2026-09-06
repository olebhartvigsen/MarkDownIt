// Layout phase 3: normalize long edges via dummy nodes.
// Emulates dagre's makeSpaceForEdgeLabels: every original rank gap of 1 is
// doubled to 2 (via mid-rank dummy), a span-k edge becomes 2k unit segments
// through (2k-1) dummies. Pre-condition: rank ints are pre-doubled by
// AssignRanks (see layout_rank.cpp) so real nodes sit on even ranks and
// dummies on odd ranks. This gives every polyline a mid-rank control point
// and matches dagre's coordinate output at 0.5 DIP.
#include "layout_internal.h"

#include <cstdlib>

namespace mermaid {

bool AllEdgesUnitLength(const LayoutGraph& g) {
    for (const auto& e : g.edges) {
        int ru = g.nodes[static_cast<size_t>(e.from)].rank;
        int rv = g.nodes[static_cast<size_t>(e.to)].rank;
        int span = ru > rv ? ru - rv : rv - ru;
        if (span != 1) return false;
    }
    return true;
}

void Normalize(LayoutGraph& g) {
    if (!g.original_edges.empty()) return;  // idempotent
    g.original_edges = g.edges;

    std::vector<LayoutEdge> new_edges;
    new_edges.reserve(g.edges.size() * 2);
    g.dummy_chains.clear();

    for (size_t i = 0; i < g.original_edges.size(); ++i) {
        const LayoutEdge& e = g.original_edges[i];
        int ru = g.nodes[static_cast<size_t>(e.from)].rank;
        int rv = g.nodes[static_cast<size_t>(e.to)].rank;
        int span = rv - ru;
        int abs_span = span < 0 ? -span : span;
        if (abs_span <= 1) {
            // Should not happen once ranks are doubled, but keep safe.
            LayoutEdge kept = e;
            kept.original_edge_index = static_cast<int>(i);
            new_edges.push_back(kept);
            continue;
        }
        int step = span > 0 ? 1 : -1;
        DummyChain chain;
        chain.original_edge_index = static_cast<int>(i);
        int prev_id = e.from;
        int r = ru + step;
        while (r != rv) {
            LayoutNode d;
            d.id = static_cast<int>(g.nodes.size());
            d.is_dummy = true;
            d.rank = r;
            d.width = 0;
            d.height = 0;
            g.nodes.push_back(d);
            chain.dummy_nodes.push_back(d.id);

            LayoutEdge seg;
            seg.from = prev_id;
            seg.to = d.id;
            seg.reversed = e.reversed;
            seg.minlen = 1;
            seg.weight = e.weight;
            seg.original_edge_index = static_cast<int>(i);
            new_edges.push_back(seg);

            prev_id = d.id;
            r += step;
        }
        LayoutEdge tail;
        tail.from = prev_id;
        tail.to = e.to;
        tail.reversed = e.reversed;
        tail.minlen = 1;
        tail.weight = e.weight;
        tail.label = e.label;
        tail.original_edge_index = static_cast<int>(i);
        new_edges.push_back(tail);

        g.dummy_chains.push_back(std::move(chain));
    }

    g.edges = std::move(new_edges);
}

void Denormalize(LayoutGraph& g) {
    if (g.original_edges.empty()) return;
    size_t first_dummy = g.nodes.size();
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        if (g.nodes[i].is_dummy) { first_dummy = i; break; }
    }
    g.nodes.resize(first_dummy);
    g.edges = std::move(g.original_edges);
    g.original_edges.clear();
    g.dummy_chains.clear();
}

}  // namespace mermaid
