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

// DFS init order, dagre init-order.js ported 1:1: iterate ALL nodes sorted by
// (rank, insertion order), DFS from each unvisited, push to layers at first
// visit. Successors follow edge-list order (our edge indices already match
// dagre's normalize insertion order).
void InitOrderDFS(const LayoutGraph& g, RankLayers& L) {
    std::vector<std::vector<int>> succ(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.from >= 0 && e.to >= 0)
            succ[static_cast<size_t>(e.from)].push_back(e.to);
    }
    std::vector<char> seen(g.nodes.size(), 0);
    for (auto& row : L.ranks) row.clear();

    // Nodes sorted by rank, stable within rank = insertion order.
    std::vector<int> ordered;
    ordered.reserve(g.nodes.size());
    for (const auto& n : g.nodes) ordered.push_back(n.id);
    std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
        return g.nodes[static_cast<size_t>(a)].rank <
               g.nodes[static_cast<size_t>(b)].rank;
    });

    // Iterative preorder DFS (matches dagre's recursive dfs visit order).
    std::vector<int> stack;
    for (int root : ordered) {
        if (seen[static_cast<size_t>(root)]) continue;
        stack.push_back(root);
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();
            if (seen[static_cast<size_t>(u)]) continue;
            seen[static_cast<size_t>(u)] = 1;
            int r = g.nodes[static_cast<size_t>(u)].rank;
            if (r >= 0) L.ranks[static_cast<size_t>(r)].push_back(u);
            // Push successors in reverse so the forward-most is on top.
            auto& kids = succ[static_cast<size_t>(u)];
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                if (!seen[static_cast<size_t>(*it)]) stack.push_back(*it);
            }
        }
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

// ---------------------------------------------------------------------------
// dagre order phase, ported 1:1 from dagre-d3-es/src/dagre/order/*.
//
// dagre uses a weighted BARYCENTER (mean of neighbor positions, weighted by
// edge weight), not a median. Each sweep rebuilds a per-rank layer graph,
// sorts it with sortSubgraph -> resolveConflicts -> sort(compareWithBias),
// and the whole loop runs sweeps until 4 iterations pass without a crossing
// improvement, keeping the best layering seen. Tie-breaking is by the entry's
// original index i (left bias) or reversed (right bias) - this is what makes
// dagre pick its exact permutation on ambiguous graphs like 04-crossing.
// ---------------------------------------------------------------------------

struct Entry {
    std::vector<int> vs;      // aggregated node ids (singleton unless merged)
    int i = 0;                // lowest original index in vs
    bool has_bc = false;
    double barycenter = 0.0;
    double weight = 0.0;
};

// barycenter.js: mean of neighbor orders weighted by edge weight, restricted
// to the layer-graph edges: PREDECESSORS on down-sweeps (inEdges relation),
// SUCCESSORS on up-sweeps (outEdges relation). Nodes with no adjacent-rank
// neighbors get no barycenter.
static std::vector<Entry> BarycenterEntries(const LayoutGraph& g,
                                            const std::vector<int>& movable,
                                            const std::vector<int>& pos_of,
                                            bool use_preds) {
    std::vector<Entry> out;
    out.reserve(movable.size());
    for (size_t idx = 0; idx < movable.size(); ++idx) {
        int v = movable[static_cast<size_t>(idx)];
        Entry en;
        en.vs = {v};
        en.i = static_cast<int>(idx);
        double sum = 0.0, weight = 0.0;
        for (const auto& e : g.edges) {
            int nbr;
            if (use_preds) {
                if (e.to != v) continue;
                nbr = e.from;
            } else {
                if (e.from != v) continue;
                nbr = e.to;
            }
            int p = pos_of[static_cast<size_t>(nbr)];
            if (p < 0) continue;
            sum += static_cast<double>(e.weight) * static_cast<double>(p);
            weight += static_cast<double>(e.weight);
        }
        if (weight > 0.0) {
            en.has_bc = true;
            en.barycenter = sum / weight;
            en.weight = weight;
        }
        out.push_back(std::move(en));
    }
    return out;
}

// sort.js compareWithBias: by barycenter, ties by i (left) or reversed (right).
static void SortEntries(std::vector<Entry>& entries, bool bias_right) {
    std::stable_sort(entries.begin(), entries.end(),
                     [bias_right](const Entry& a, const Entry& b) {
                         if (a.has_bc && b.has_bc &&
                             a.barycenter != b.barycenter) {
                             return a.barycenter < b.barycenter;
                         }
                         if (a.has_bc != b.has_bc) return a.has_bc;
                         return bias_right ? (a.i > b.i) : (a.i < b.i);
                     });
}

// sort.js: partition entries into sortable (with barycenter) and unsortable;
// sort the sortable ones; then weave unsortable entries back in at their
// original index positions (consumeUnsortable), lowest remaining i first.
static std::vector<Entry> SortEntriesWeaved(std::vector<Entry> entries,
                                            bool bias_right) {
    std::vector<Entry> sortable, unsortable;
    for (auto& e : entries) {
        if (e.has_bc) sortable.push_back(std::move(e));
        else           unsortable.push_back(std::move(e));
    }
    // unsortable = _.sortBy(unsorted, entry => -entry.i): descending by i.
    std::stable_sort(unsortable.begin(), unsortable.end(),
                     [](const Entry& a, const Entry& b) { return a.i > b.i; });
    SortEntries(sortable, bias_right);

    std::vector<Entry> vs;
    size_t vs_index = 0;
    auto consume_unsortable = [&](size_t index) {
        // While last unsortable has i <= index, pop and place it next.
        while (!unsortable.empty() && unsortable.back().i <= static_cast<int>(index)) {
            vs.push_back(std::move(unsortable.back()));
            unsortable.pop_back();
            ++index;
        }
        return index;
    };
    vs_index = consume_unsortable(vs_index);
    for (auto& en : sortable) {
        vs_index += en.vs.size();
        vs.push_back(std::move(en));
        vs_index = consume_unsortable(vs_index);
    }
    // Any leftovers (indices beyond vs end) append in reverse-pop order.
    while (!unsortable.empty()) {
        vs.push_back(std::move(unsortable.back()));
        unsortable.pop_back();
    }
    return vs;
}

// Per-rank layer graph in dagre = only edges between r-1 (or r+1) and r.
// movable = ALL nodes of rank r in GLOBAL INSERTION ORDER (dagre's
// g.children(root) for the per-rank layer graph, which is built by
// iterating g.nodes() in insertion order). The sort tie-break index i is
// exactly the entry's position in this list.
static void SweepRankDagre(const LayoutGraph& g, RankLayers& L, int r,
                           bool use_preds, bool bias_right) {
    if (r < 0 || r >= static_cast<int>(L.ranks.size())) return;
    int adj = use_preds ? r - 1 : r + 1;
    if (adj < 0 || adj >= static_cast<int>(L.ranks.size())) return;

    int max_id = 0;
    for (const auto& n : g.nodes) if (n.id > max_id) max_id = n.id;
    std::vector<int> pos_of(static_cast<size_t>(max_id + 1), -1);
    for (size_t i = 0; i < L.ranks[static_cast<size_t>(adj)].size(); ++i)
        pos_of[static_cast<size_t>(L.ranks[static_cast<size_t>(adj)][i])] = static_cast<int>(i);

    // Movable: the rank's nodes in global insertion order (id order == the
    // order our Normalize inserted dummies). NOTE: use only nodes present
    // in this rank (L.ranks[r] always holds them all).
    std::vector<int> present(static_cast<size_t>(max_id + 1), 0);
    for (int id : L.ranks[static_cast<size_t>(r)]) present[static_cast<size_t>(id)] = 1;
    std::vector<int> movable;
    movable.reserve(L.ranks[static_cast<size_t>(r)].size());
    for (const auto& n : g.nodes) {
        if (present[static_cast<size_t>(n.id)]) movable.push_back(n.id);
    }

    std::vector<Entry> entries = BarycenterEntries(g, movable, pos_of, use_preds);

    // dagre has a resolveConflicts pass over a layout-constraint graph; with
    // no subgraph parents it is a no-op preserving entry order, so the sort
    // input equals the barycenter entries list.
    std::vector<Entry> ordered = SortEntriesWeaved(std::move(entries), bias_right);

    auto& row = L.ranks[static_cast<size_t>(r)];
    std::vector<int> new_row;
    new_row.reserve(row.size());
    for (const auto& en : ordered)
        for (int v : en.vs) new_row.push_back(v);
    row = std::move(new_row);
}

// order() in dagre index.js: sweeps until 4 non-improving iterations, keeping
// best. IMPORTANT dagre semantics: the sweep ALWAYS keeps its result (no
// rollback); "best" only records the layering snapshot. bestCC starts at
// +infinity, so iteration 0 is always recorded.
void OrderSweepsDagre(const LayoutGraph& g, RankLayers& L) {
    double best_cc = std::numeric_limits<double>::infinity();
    RankLayers best;
    int last_best = 0;
    for (int i = 0; last_best < 4; ++i, ++last_best) {
        bool down = (i % 2 == 1);          // dagre: i%2 ? down : up
        bool bias_right = (i % 4 >= 2);    // dagre: i%4 >= 2
        if (down) {
            for (int r = 1; r < static_cast<int>(L.ranks.size()); ++r)
                SweepRankDagre(g, L, r, true, bias_right);
        } else {
            for (int r = static_cast<int>(L.ranks.size()) - 2; r >= 0; --r)
                SweepRankDagre(g, L, r, false, bias_right);
        }
        int cc = CountCrossingsLayers(g, L);
        if (cc < best_cc) {
            last_best = 0;
            best_cc = cc;
            best = L;
        }
    }
    if (best.ranks.empty()) best = L;
    L = best;
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

    // dagre's exact sweep machinery: barycenter sort with bias, stopping after
    // 4 non-improving iterations, keeping the best crossing count. No adjacent
    // transposition (dagre has none).
    OrderSweepsDagre(g, L);
    WriteOrders(g, L);
}

}  // namespace mermaid
