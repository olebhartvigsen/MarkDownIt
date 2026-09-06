// Layout phase 3: normalize long edges via dummy nodes.
// Task 7: split any edge that spans more than one rank into unit-length
// segments, chained through zero-size dummy nodes. Those dummies become
// the bend points during routing (Task 10).
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
    new_edges.reserve(g.edges.size());
    g.dummy_chains.clear();

    for (size_t i = 0; i < g.original_edges.size(); ++i) {
        const LayoutEdge& e = g.original_edges[i];
        int ru = g.nodes[static_cast<size_t>(e.from)].rank;
        int rv = g.nodes[static_cast<size_t>(e.to)].rank;
        int span = rv - ru;
        int abs_span = span < 0 ? -span : span;
        if (abs_span <= 1) {
            LayoutEdge kept = e;
            kept.original_edge_index = static_cast<int>(i);
            new_edges.push_back(kept);
            continue;
        }
        // Multi-rank edge: create dummies at each intermediate rank.
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
    // Drop dummies from the node list. Dummy ids were appended after the
    // real nodes, so we can just truncate down to the first dummy.
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
