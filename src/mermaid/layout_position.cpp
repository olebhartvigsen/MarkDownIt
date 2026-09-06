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

// Sweep in one direction. neighbors_up=true uses parents (rank r-1),
// false uses children (rank r+1). x values grow monotonically.
void Sweep(LayoutGraph& g,
           const std::vector<std::vector<int>>& rows,
           const std::vector<std::vector<int>>& parents,
           const std::vector<std::vector<int>>& children,
           double node_sep,
           bool top_down) {
    int r_start, r_end, r_step;
    if (top_down) { r_start = 0; r_end = static_cast<int>(rows.size()); r_step = 1; }
    else { r_start = static_cast<int>(rows.size()) - 1; r_end = -1; r_step = -1; }

    for (int r = r_start; r != r_end; r += r_step) {
        const auto& row = rows[static_cast<size_t>(r)];
        double left_bound = -std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < row.size(); ++k) {
            int u = row[k];
            double w = g.nodes[u].width;
            const auto& nbrs = top_down ? parents[u] : children[u];
            std::vector<double> nx;
            nx.reserve(nbrs.size());
            for (int v : nbrs) nx.push_back(g.nodes[v].x);
            double cand_median = nbrs.empty() ? 0.0 : Median(nx);
            double cand_self   = w * 0.5;
            double cand_left   = (k == 0) ? w * 0.5 : (left_bound + node_sep + w * 0.5);
            double cur = g.nodes[u].x;
            double x = cur;
            if (cand_median > x) x = cand_median;
            if (cand_self   > x) x = cand_self;
            if (cand_left   > x) x = cand_left;
            g.nodes[u].x = static_cast<float>(x);
            left_bound = x + w * 0.5;
        }
    }
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

    // ---- X: two-pass median with monotonic max ----
    std::vector<std::vector<int>> parents(g.nodes.size());
    std::vector<std::vector<int>> children(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        children[static_cast<size_t>(e.from)].push_back(e.to);
        parents [static_cast<size_t>(e.to)  ].push_back(e.from);
    }

    for (auto& n : g.nodes) n.x = 0.0f;

    Sweep(g, rows, parents, children, p.node_sep, /*top_down=*/true);
    Sweep(g, rows, parents, children, p.node_sep, /*top_down=*/false);

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
