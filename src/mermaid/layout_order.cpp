// src/mermaid/layout_order.cpp
// Task 8: layout ordering / crossing minimization.
// DFS-based init order, then 8 iterations of median heuristic with
// alternating sweep direction, followed by adjacent transposition.
// Best crossing count wins.
#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace mermaid {

namespace {

struct RankLayers {
    // ranks[r] = vector of node ids at rank r, in current order.
    std::vector<std::vector<int>> ranks;
};

RankLayers BuildLayers(const LayoutGraph& g) {
    RankLayers L;
    int max_rank = -1;
    for (const auto& n : g.nodes) if (n.rank > max_rank) max_rank = n.rank;
    if (max_rank < 0) return L;
    L.ranks.assign(static_cast<size_t>(max_rank + 1), {});
    for (const auto& n : g.nodes) {
        if (n.rank >= 0) L.ranks[static_cast<size_t>(n.rank)].push_back(n.id);
    }
    return L;
}

void WriteOrders(LayoutGraph& g, const RankLayers& L) {
    for (const auto& row : L.ranks) {
        for (size_t i = 0; i < row.size(); ++i) {
            g.nodes[static_cast<size_t>(row[i])].order = static_cast<int>(i);
        }
    }
}

// DFS visit from sources (rank 0). Assign per-rank order in visit order.
void InitOrderDFS(const LayoutGraph& g, RankLayers& L) {
    std::vector<std::vector<int>> out(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from >= 0 && e.to >= 0)
            out[static_cast<size_t>(e.from)].push_back(e.to);
    }
    std::vector<char> seen(g.nodes.size(), 0);
    for (auto& row : L.ranks) row.clear();

    // Sort sources by id for determinism.
    std::vector<int> sources;
    for (const auto& n : g.nodes) if (n.rank == 0) sources.push_back(n.id);
    std::sort(sources.begin(), sources.end());

    // Iterative DFS.
    std::vector<int> stack;
    auto visit_root = [&](int root) {
        if (seen[static_cast<size_t>(root)]) return;
        stack.push_back(root);
        while (!stack.empty()) {
            int u = stack.back(); stack.pop_back();
            if (seen[static_cast<size_t>(u)]) continue;
            seen[static_cast<size_t>(u)] = 1;
            int r = g.nodes[static_cast<size_t>(u)].rank;
            if (r >= 0) L.ranks[static_cast<size_t>(r)].push_back(u);
            // push children in reverse so lower ids get visited first
            auto& kids = out[static_cast<size_t>(u)];
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                if (!seen[static_cast<size_t>(*it)]) stack.push_back(*it);
            }
        }
    };
    for (int s : sources) visit_root(s);
    // Any node not reached (shouldn't happen post-normalize, but be safe).
    for (const auto& n : g.nodes) {
        if (!seen[static_cast<size_t>(n.id)] && n.rank >= 0)
            L.ranks[static_cast<size_t>(n.rank)].push_back(n.id);
    }
}

// Count crossings between two adjacent ranks given orders.
int CountCrossingsBetween(const std::vector<int>& upper_order,
                          const std::vector<int>& lower_order,
                          const std::vector<std::pair<int,int>>& edges_uv) {
    // For each edge (u,v), get positions in upper/lower.
    std::vector<int> upos(upper_order.size(), -1), lpos(lower_order.size(), -1);
    // We need position by node id; but orders hold node ids -> position.
    // Build maps from node id -> pos. Use max id to size.
    (void)upos; (void)lpos;
    // Build edge position pairs.
    // We'll do a simple O(E^2) inversion count.
    // Map id -> position.
    // Since node ids are arbitrary ints, use a small hash via sorted lookup.
    // Simpler: build lookup vectors indexed by id.
    int max_id = -1;
    for (int id : upper_order) if (id > max_id) max_id = id;
    for (int id : lower_order) if (id > max_id) max_id = id;
    for (auto& p : edges_uv) { if (p.first > max_id) max_id = p.first; if (p.second > max_id) max_id = p.second; }
    std::vector<int> upos2(static_cast<size_t>(max_id + 1), -1);
    std::vector<int> lpos2(static_cast<size_t>(max_id + 1), -1);
    for (size_t i = 0; i < upper_order.size(); ++i)
        upos2[static_cast<size_t>(upper_order[i])] = static_cast<int>(i);
    for (size_t i = 0; i < lower_order.size(); ++i)
        lpos2[static_cast<size_t>(lower_order[i])] = static_cast<int>(i);
    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(edges_uv.size());
    for (auto& p : edges_uv) {
        int a = upos2[static_cast<size_t>(p.first)];
        int b = lpos2[static_cast<size_t>(p.second)];
        if (a < 0 || b < 0) continue;
        pairs.emplace_back(a, b);
    }
    int cross = 0;
    for (size_t i = 0; i < pairs.size(); ++i) {
        for (size_t j = i + 1; j < pairs.size(); ++j) {
            if ((pairs[i].first < pairs[j].first && pairs[i].second > pairs[j].second) ||
                (pairs[i].first > pairs[j].first && pairs[i].second < pairs[j].second)) {
                ++cross;
            }
        }
    }
    return cross;
}

int CountCrossingsLayers(const LayoutGraph& g, const RankLayers& L) {
    if (L.ranks.size() < 2) return 0;
    // Group edges by upper rank.
    std::vector<std::vector<std::pair<int,int>>> by_rank(L.ranks.size());
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        int ru = g.nodes[static_cast<size_t>(e.from)].rank;
        int rv = g.nodes[static_cast<size_t>(e.to)].rank;
        if (ru < 0 || rv < 0) continue;
        int upper = ru, lower = rv;
        int u = e.from, v = e.to;
        if (upper > lower) { std::swap(upper, lower); std::swap(u, v); }
        if (lower != upper + 1) continue;  // only unit-length edges after normalize
        by_rank[static_cast<size_t>(upper)].emplace_back(u, v);
    }
    int total = 0;
    for (size_t r = 0; r + 1 < L.ranks.size(); ++r) {
        total += CountCrossingsBetween(L.ranks[r], L.ranks[r + 1], by_rank[r]);
    }
    return total;
}

// For each node on `rank`, compute median of neighbor positions on `adj_rank`.
// Returns NaN if node has no neighbors on adjacent rank (leave in place).
double MedianOf(const std::vector<int>& positions) {
    if (positions.empty()) return std::nan("");
    std::vector<int> p = positions;
    std::sort(p.begin(), p.end());
    size_t n = p.size();
    if (n % 2 == 1) return static_cast<double>(p[n / 2]);
    // Weighted median (dagre style): weight by side widths.
    double left = static_cast<double>(p[n / 2 - 1]);
    double right = static_cast<double>(p[n / 2]);
    if (n == 2) return (left + right) / 2.0;
    double left_w = left - static_cast<double>(p[0]);
    double right_w = static_cast<double>(p[n - 1]) - right;
    if (left_w + right_w == 0) return (left + right) / 2.0;
    return (left * right_w + right * left_w) / (left_w + right_w);
}

void MedianSweep(const LayoutGraph& g, RankLayers& L, bool down) {
    // adjacency: for each node id, list of neighbor ids on adjacent rank
    // Depending on sweep direction we look at predecessors (down sweep) or
    // successors (up sweep).
    std::vector<std::vector<int>> preds(g.nodes.size()), succs(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        preds[static_cast<size_t>(e.to)].push_back(e.from);
        succs[static_cast<size_t>(e.from)].push_back(e.to);
    }

    auto sweep_rank = [&](int r, bool use_preds) {
        if (r < 0 || r >= static_cast<int>(L.ranks.size())) return;
        int adj = use_preds ? r - 1 : r + 1;
        if (adj < 0 || adj >= static_cast<int>(L.ranks.size())) return;
        // Positions on adj rank.
        int max_id = 0;
        for (const auto& row : L.ranks) for (int id : row) if (id > max_id) max_id = id;
        std::vector<int> pos(static_cast<size_t>(max_id + 1), -1);
        for (size_t i = 0; i < L.ranks[static_cast<size_t>(adj)].size(); ++i)
            pos[static_cast<size_t>(L.ranks[static_cast<size_t>(adj)][i])] = static_cast<int>(i);
        auto& row = L.ranks[static_cast<size_t>(r)];
        std::vector<std::pair<double,int>> keyed;  // (median, current_index)
        keyed.reserve(row.size());
        for (size_t i = 0; i < row.size(); ++i) {
            int id = row[i];
            const auto& nbrs = use_preds ? preds[static_cast<size_t>(id)]
                                         : succs[static_cast<size_t>(id)];
            std::vector<int> positions;
            for (int nb : nbrs) {
                int p = pos[static_cast<size_t>(nb)];
                if (p >= 0) positions.push_back(p);
            }
            double m = MedianOf(positions);
            keyed.emplace_back(m, static_cast<int>(i));
        }
        // Stable sort: NaN entries keep their current position.
        // Achieve by using current index as secondary key and treating NaN
        // as +infinity to sort them last? Dagre keeps them in place; we
        // approximate by using their current index as the median.
        for (auto& kv : keyed) {
            if (std::isnan(kv.first)) kv.first = static_cast<double>(kv.second);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const std::pair<double,int>& a, const std::pair<double,int>& b) {
                             return a.first < b.first;
                         });
        std::vector<int> new_row(row.size());
        for (size_t i = 0; i < keyed.size(); ++i) new_row[i] = row[static_cast<size_t>(keyed[i].second)];
        row = std::move(new_row);
    };

    if (down) {
        // For a down sweep, we fix rank 0 and reorder subsequent ranks based
        // on predecessors above.
        for (int r = 1; r < static_cast<int>(L.ranks.size()); ++r) sweep_rank(r, true);
    } else {
        for (int r = static_cast<int>(L.ranks.size()) - 2; r >= 0; --r) sweep_rank(r, false);
    }
}

bool AdjacentTranspose(const LayoutGraph& g, RankLayers& L) {
    bool improved_any = false;
    bool improved = true;
    int guard = 0;
    while (improved && guard++ < 32) {
        improved = false;
        int cur = CountCrossingsLayers(g, L);
        for (size_t r = 0; r < L.ranks.size(); ++r) {
            auto& row = L.ranks[r];
            for (size_t i = 0; i + 1 < row.size(); ++i) {
                std::swap(row[i], row[i + 1]);
                int c2 = CountCrossingsLayers(g, L);
                if (c2 < cur) {
                    cur = c2;
                    improved = true;
                    improved_any = true;
                } else {
                    std::swap(row[i], row[i + 1]);
                }
            }
        }
    }
    return improved_any;
}

}  // namespace

int CountCrossings(const LayoutGraph& g) {
    RankLayers L = BuildLayers(g);
    // Sort each rank by current `order` if it's set; else by id.
    for (auto& row : L.ranks) {
        std::sort(row.begin(), row.end(), [&](int a, int b) {
            int oa = g.nodes[static_cast<size_t>(a)].order;
            int ob = g.nodes[static_cast<size_t>(b)].order;
            if (oa < 0 && ob < 0) return a < b;
            if (oa < 0) return false;
            if (ob < 0) return true;
            return oa < ob;
        });
    }
    return CountCrossingsLayers(g, L);
}

void Order(LayoutGraph& g) {
    RankLayers L = BuildLayers(g);
    if (L.ranks.empty()) return;
    InitOrderDFS(g, L);

    RankLayers best = L;
    int best_cross = CountCrossingsLayers(g, L);

    for (int iter = 0; iter < 8; ++iter) {
        bool down = (iter % 2 == 0);
        MedianSweep(g, L, down);
        AdjacentTranspose(g, L);
        int c = CountCrossingsLayers(g, L);
        if (c < best_cross) {
            best_cross = c;
            best = L;
        }
    }
    WriteOrders(g, best);
}

}  // namespace mermaid
