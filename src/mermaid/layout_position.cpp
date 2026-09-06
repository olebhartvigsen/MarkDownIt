// src/mermaid/layout_position.cpp
//
// Task 9 - Phase 5: coordinate assignment.
//
// Y coordinates: pure function of rank. Row height is max node height on
// that rank. Node y is the center of its row.
//
// X coordinates: damped relaxation with dagre-style asymmetric sep.
// Real nodes get node_sep/2 own margin, dummies get edge_sep/2, so gaps
// between two reals sum to node_sep, gap between two dummies to edge_sep,
// gap between real and dummy to (node_sep + edge_sep) / 2. Each iteration:
//  1. compute ideal x per node = mean of parent+child x
//  2. tight-pack each rank in Order sequence using the asymmetric sep
//  3. shift the tight-packed row so its mean matches the mean of ideals
//  4. blend toward the shifted target with damping factor 0.5
// The result approximates dagre's Brandes-Koepf output within a few DIP
// on balanced graphs and is exact on chains and sibling fans.

#include "layout_internal.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace mermaid {

namespace {

int MaxRank(const LayoutGraph& g) {
    int r = -1;
    for (const auto& n : g.nodes) if (n.rank > r) r = n.rank;
    return r;
}

std::vector<std::vector<int>> NodesByRank(const LayoutGraph& g) {
    int rmax = MaxRank(g);
    std::vector<std::vector<int>> rows(rmax < 0 ? 0 : rmax + 1);
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        int r = g.nodes[i].rank;
        if (r < 0) continue;
        rows[static_cast<size_t>(r)].push_back(static_cast<int>(i));
    }
    for (auto& row : rows) {
        std::sort(row.begin(), row.end(), [&](int a, int b) {
            int oa = g.nodes[a].order, ob = g.nodes[b].order;
            if (oa != ob) return oa < ob;
            return g.nodes[a].id < g.nodes[b].id;
        });
    }
    return rows;
}

// Half-margin dagre uses on either side of a node: node_sep/2 for a real
// node, edge_sep/2 for a dummy. Two adjacent margins summed = gap between
// the pair.
static inline double HalfSep(const LayoutNode& n, double node_sep, double edge_sep) {
    return 0.5 * (n.is_dummy ? edge_sep : node_sep);
}

// Compute rank-relative tight-pack centres starting the row at x=0.
static std::vector<double> TightPack(const LayoutGraph& g,
                                     const std::vector<int>& row,
                                     double node_sep, double edge_sep) {
    std::vector<double> c(row.size(), 0.0);
    if (row.empty()) return c;
    c[0] = g.nodes[row[0]].width * 0.5;
    for (size_t k = 1; k < row.size(); ++k) {
        const auto& a = g.nodes[row[k - 1]];
        const auto& b = g.nodes[row[k]];
        double gap = HalfSep(a, node_sep, edge_sep) + HalfSep(b, node_sep, edge_sep);
        c[k] = c[k - 1] + a.width * 0.5 + gap + b.width * 0.5;
    }
    return c;
}

}  // namespace

void AssignCoordinates(LayoutGraph& g, const LayoutParams& p) {
    if (g.nodes.empty()) return;

    auto rows = NodesByRank(g);

    // ---- Y from rank ----
    std::vector<double> row_h(rows.size(), 0.0);
    for (size_t r = 0; r < rows.size(); ++r) {
        double h = 0.0;
        for (int u : rows[r]) h = std::max(h, static_cast<double>(g.nodes[u].height));
        row_h[r] = h;
    }
    std::vector<double> row_center(rows.size(), 0.0);
    double cursor = 0.0;
    for (size_t r = 0; r < rows.size(); ++r) {
        row_center[r] = cursor + row_h[r] * 0.5;
        cursor += row_h[r] + p.rank_sep;
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        for (int u : rows[r]) g.nodes[u].y = static_cast<float>(row_center[r]);
    }

    // ---- X: build neighbour lists ----
    std::vector<std::vector<int>> parents(g.nodes.size());
    std::vector<std::vector<int>> children(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        children[static_cast<size_t>(e.from)].push_back(e.to);
        parents [static_cast<size_t>(e.to)  ].push_back(e.from);
    }

    // Seed: tight-pack every rank from 0.
    std::vector<double> x(g.nodes.size(), 0.0);
    for (const auto& row : rows) {
        auto tight = TightPack(g, row, p.node_sep, p.edge_sep);
        for (size_t k = 0; k < row.size(); ++k) x[row[k]] = tight[k];
    }

    // Damped relaxation. 0.5 damping avoids oscillation; ~120 iterations
    // is more than enough for typical flowcharts.
    const double damp = 0.5;
    for (int iter = 0; iter < 128; ++iter) {
        std::vector<double> ideal(g.nodes.size(), 0.0);
        for (size_t u = 0; u < g.nodes.size(); ++u) {
            double s = 0.0; int c = 0;
            for (int v : parents[u])  { s += x[v]; ++c; }
            for (int v : children[u]) { s += x[v]; ++c; }
            ideal[u] = (c > 0) ? (s / c) : x[u];
        }
        for (const auto& row : rows) {
            if (row.empty()) continue;
            auto tight = TightPack(g, row, p.node_sep, p.edge_sep);
            double mean_ideal = 0.0, mean_tight = 0.0;
            for (size_t k = 0; k < row.size(); ++k) {
                mean_ideal += ideal[row[k]];
                mean_tight += tight[k];
            }
            mean_ideal /= static_cast<double>(row.size());
            mean_tight /= static_cast<double>(row.size());
            double shift = mean_ideal - mean_tight;
            for (size_t k = 0; k < row.size(); ++k) {
                double target = tight[k] + shift;
                x[row[k]] = x[row[k]] + damp * (target - x[row[k]]);
            }
        }
    }

    for (size_t i = 0; i < g.nodes.size(); ++i) {
        g.nodes[i].x = static_cast<float>(x[i]);
    }

    // Left-align: shift so leftmost real node's left edge sits at 0. Dummies
    // are ignored in the min so dummy positioning does not skew the bbox.
    double min_left = std::numeric_limits<double>::infinity();
    for (const auto& n : g.nodes) {
        if (n.is_dummy) continue;
        double left = static_cast<double>(n.x) - static_cast<double>(n.width) * 0.5;
        if (left < min_left) min_left = left;
    }
    if (min_left != 0.0 && min_left != std::numeric_limits<double>::infinity()) {
        for (auto& n : g.nodes) n.x = static_cast<float>(n.x - min_left);
    }
}

}  // namespace mermaid
