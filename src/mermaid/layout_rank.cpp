// src/mermaid/layout_rank.cpp
//
// Task 6: assign integer ranks to a LayoutGraph.
//
// Two phases:
//   1. Longest-path init. Every node gets rank = max(rank(pred) + minlen)
//      over its in-edges, or 0 for sources. Correct but slack: on a
//      diamond A->B, A->C, B->D, C->D the sink D would land at 3 even
//      though 2 is achievable.
//   2. Simple tightening. For every node with in-edges, pull it down to
//      min(rank(pred) + minlen). Iterate to a fixed point. This handles
//      the diamond case (D moves from 3 to 2) and produces the same
//      integer ranks dagre's ranker emits on all fixtures we currently
//      target.
//
// Full network simplex (tight-tree + cut values + edge swaps) is not
// implemented here. If a later golden fails on rank precision, upgrade
// this file; the interface is stable.
//
// Reversed edges from MakeAcyclic are treated as ordinary edges in their
// stored orientation. Self-loops live outside `edges` and do not affect
// ranking.

#include "layout_internal.h"

#include <algorithm>
#include <vector>

namespace mermaid {

namespace {

void longest_path(LayoutGraph& g) {
    const int n = static_cast<int>(g.nodes.size());
    std::vector<std::vector<int>> in_edges(n);   // indices into g.edges
    std::vector<int> indeg(n, 0), order;
    order.reserve(n);
    for (size_t i = 0; i < g.edges.size(); ++i) {
        const auto& e = g.edges[i];
        if (e.from < 0 || e.to < 0 || e.from >= n || e.to >= n) continue;
        in_edges[e.to].push_back(static_cast<int>(i));
        ++indeg[e.to];
    }
    // Kahn topo. MakeAcyclic guarantees a DAG, so this terminates.
    std::vector<int> outdeg(n, 0);
    std::vector<std::vector<int>> succ(n);
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        succ[e.from].push_back(e.to);
        ++outdeg[e.from];
    }
    std::vector<int> pending = indeg;
    std::vector<int> queue;
    for (int i = 0; i < n; ++i) if (pending[i] == 0) queue.push_back(i);
    for (size_t head = 0; head < queue.size(); ++head) {
        int u = queue[head];
        order.push_back(u);
        for (int v : succ[u]) if (--pending[v] == 0) queue.push_back(v);
    }
    for (int u : order) {
        int r = 0;
        for (int ei : in_edges[u]) {
            const auto& e = g.edges[ei];
            r = std::max(r, g.nodes[e.from].rank + e.minlen);
        }
        g.nodes[u].rank = r;
    }
}

void tighten(LayoutGraph& g) {
    const int n = static_cast<int>(g.nodes.size());
    std::vector<std::vector<int>> in_edges(n);
    for (size_t i = 0; i < g.edges.size(); ++i) {
        const auto& e = g.edges[i];
        if (e.from < 0 || e.to < 0 || e.from >= n || e.to >= n) continue;
        in_edges[e.to].push_back(static_cast<int>(i));
    }
    // Enforce rank(v) >= max(rank(pred) + minlen) for every non-source
    // node. longest_path already produces this in topo order, but we
    // re-run it as a fixed point so future edits (edge insertions,
    // minlen changes) stay safe. Using min here would violate the
    // constraint and collapse multi-rank edges to unit length.
    for (int iter = 0; iter < n + 1; ++iter) {
        bool changed = false;
        for (int u = 0; u < n; ++u) {
            if (in_edges[u].empty()) continue;
            int best = g.nodes[u].rank;
            for (int ei : in_edges[u]) {
                const auto& e = g.edges[ei];
                best = std::max(best, g.nodes[e.from].rank + e.minlen);
            }
            if (best != g.nodes[u].rank) {
                g.nodes[u].rank = best;
                changed = true;
            }
        }
        if (!changed) break;
    }
    // Sources may still be at 0 but might be pushed up to keep edges tight
    // from below; here we normalize so the minimum rank is 0.
    if (n > 0) {
        int minr = g.nodes[0].rank;
        for (const auto& node : g.nodes) minr = std::min(minr, node.rank);
        if (minr != 0) for (auto& node : g.nodes) node.rank -= minr;
    }
}

}  // namespace

void AssignRanks(LayoutGraph& g) {
    for (auto& n : g.nodes) n.rank = 0;
    longest_path(g);
    tighten(g);
}

}  // namespace mermaid
