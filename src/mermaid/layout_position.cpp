// src/mermaid/layout_position.cpp
//
// Task 9 - Phase 5: coordinate assignment.
//
// Y coordinates: pure function of rank. Row height is max node height on
// that rank. Node y is the center of its row.
//
// X coordinates: median based approximation of Brandes-Koepf. Two passes
// (top-down using parent medians, bottom-up using child medians), each
// pass taking a monotonic max against the current value, the node's own
// half-width (left margin), and the left-sibling boundary. Finally shift
// all x so that the leftmost node's left edge sits at zero.

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

// Nodes at each rank, sorted by order (falls back to id when unset).
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

double Median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n == 0) return 0.0;
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Compute a single monotonic left-to-right pass where each node's target
// x is the median of the neighbors passed in. Row order comes from
// LayoutNode.order (set by Order()); tie-broken by id. Non-overlapping.
// Returns per-node x for THIS pass; other node.x fields are left alone.
static std::vector<double> PassCenter(const LayoutGraph& g,
                                      const std::vector<std::vector<int>>& rows,
                                      const std::vector<std::vector<int>>& neigh,
                                      const std::vector<double>& seed_x,
                                      double node_sep,
                                      bool top_down) {
    std::vector<double> x(g.nodes.size(), 0.0);
    int r_start, r_end, r_step;
    if (top_down) { r_start = 0; r_end = static_cast<int>(rows.size()); r_step = 1; }
    else { r_start = static_cast<int>(rows.size()) - 1; r_end = -1; r_step = -1; }
    for (int r = r_start; r != r_end; r += r_step) {
        const auto& row = rows[static_cast<size_t>(r)];
        double left_bound = -std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < row.size(); ++k) {
            int u = row[k];
            double w = g.nodes[u].width;
            std::vector<double> nx;
            nx.reserve(neigh[u].size());
            for (int v : neigh[u]) nx.push_back(seed_x[v]);
            double ideal = nx.empty() ? seed_x[u] : Median(nx);
            double cand_left = (k == 0)
                ? -std::numeric_limits<double>::infinity()
                : (left_bound + node_sep + w * 0.5);
            double xu = ideal;
            if (cand_left > xu) xu = cand_left;
            x[u] = xu;
            left_bound = xu + w * 0.5;
        }
    }
    return x;
}

// One iteration of "ideal + tight-pack + rank-center" positioning:
//  1. For every node, take the mean of parent+child centers from prev_x.
//  2. For each rank, tight-pack nodes in their order slot.
//  3. Shift the tight-packed row so its mean matches the mean of ideals.
// This converges quickly on symmetric layouts like diamond/crossing and
// leaves single-node rows at their exact ideal x.
static std::vector<double> IdealPackShift(const LayoutGraph& g,
                                          const std::vector<std::vector<int>>& rows,
                                          const std::vector<std::vector<int>>& parents,
                                          const std::vector<std::vector<int>>& children,
                                          const std::vector<double>& prev_x,
                                          double node_sep) {
    std::vector<double> ideal(g.nodes.size(), 0.0);
    for (size_t u = 0; u < g.nodes.size(); ++u) {
        double s = 0.0; int c = 0;
        for (int v : parents[u])  { s += prev_x[v]; ++c; }
        for (int v : children[u]) { s += prev_x[v]; ++c; }
        ideal[u] = (c > 0) ? (s / c) : prev_x[u];
    }

    std::vector<double> out(g.nodes.size(), 0.0);
    for (const auto& row : rows) {
        // Tight pack row in Order-defined sequence.
        std::vector<double> tight(row.size(), 0.0);
        double cursor = 0.0;
        for (size_t k = 0; k < row.size(); ++k) {
            double w = g.nodes[row[k]].width;
            tight[k] = cursor + w * 0.5;
            cursor += w + node_sep;
        }
        double mean_tight = 0.0, mean_ideal = 0.0;
        for (size_t k = 0; k < row.size(); ++k) {
            mean_tight += tight[k];
            mean_ideal += ideal[row[k]];
        }
        if (!row.empty()) {
            mean_tight /= static_cast<double>(row.size());
            mean_ideal /= static_cast<double>(row.size());
        }
        double shift = mean_ideal - mean_tight;
        for (size_t k = 0; k < row.size(); ++k) {
            out[row[k]] = tight[k] + shift;
        }
    }
    return out;
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

    // ---- X: seed from row order, then alternate parent-median and
    // child-median passes until stable. Each pass respects rank-order
    // spacing to keep nodes non-overlapping. Final x is the mean of the
    // two candidate alignments (top-down using parents, bottom-up using
    // children), a common Brandes-Koepf simplification.
    std::vector<std::vector<int>> parents(g.nodes.size());
    std::vector<std::vector<int>> children(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        children[static_cast<size_t>(e.from)].push_back(e.to);
        parents [static_cast<size_t>(e.to)  ].push_back(e.from);
    }

    // Seed: tight-pack each row from x=0 in Order sequence.
    std::vector<double> x(g.nodes.size(), 0.0);
    for (const auto& row : rows) {
        double cursor = 0.0;
        for (int u : row) {
            double w = g.nodes[u].width;
            x[u] = cursor + w * 0.5;
            cursor += w + p.node_sep;
        }
    }

    // Relax: pull each node toward its neighbors' mean, then re-tight-pack
    // each rank and shift the row so its mean matches the neighbors' mean.
    for (int iter = 0; iter < 24; ++iter) {
        x = IdealPackShift(g, rows, parents, children, x, p.node_sep);
    }

    for (size_t i = 0; i < g.nodes.size(); ++i) {
        g.nodes[i].x = static_cast<float>(x[i]);
    }

    // Left-align: shift so leftmost node's left edge sits at 0.
    double min_left = std::numeric_limits<double>::infinity();
    for (const auto& n : g.nodes) {
        double left = static_cast<double>(n.x) - static_cast<double>(n.width) * 0.5;
        if (left < min_left) min_left = left;
    }
    if (min_left != 0.0 && min_left != std::numeric_limits<double>::infinity()) {
        for (auto& n : g.nodes) n.x = static_cast<float>(n.x - min_left);
    }
}

}  // namespace mermaid
