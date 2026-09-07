// src/mermaid/layout_rank.cpp
//
// Task 6: assign ranks via dagre's network simplex, ported 1:1 from
// dagre-d3-es/src/dagre/rank/{util.js longestPath, feasible-tree.js,
// network-simplex.js}. Iteration order fidelity against graphlib/lodash is
// what makes the output bit-identical:
//   - nodes(): insertion order (our LayoutGraph ids are already insertion
//     order from FlowchartToLayoutGraph);
//   - edges(): insertion order; removed edges vanish from iteration and re-
//     added edges append at the end (vector + tombstone mimics JS object key
//     deletion/reinsertion);
//   - predecessors/successors of a node: insertion order of the per-side
//     maps; for the UNDIRECTED tree, edges are stored normalised (lo<hi), so
//     "preds of X" = edges where X is the higher endpoint, "sucs of X" = the
//     lower ones;
//   - neighbors(v) = union(preds, sucs): all pred-side neighbours first in
//     edge insertion order, then succ-side ones not already listed;
//   - nodeEdges(v) = inEdges ++ outEdges;
//   - lodash minBy picks the FIRST minimal element (strict < tracking).
//
// The ranker runs on the simplified graph (dagre's simplify()): parallel
// directed edges merge with weight sum + max minlen. Ranks live in a local
// vector (sources start at 0 = dagre's frame after normalizeRanks; all
// simplex maths uses rank differences only). After the first exchangeEdges
// every rank is re-derived through the tree from root's rank, so the initial
// frame's absolute anchoring washes out. Final safety: shift so min rank = 0.

#include "layout_internal.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mermaid {

namespace {

constexpr int kIntMax = std::numeric_limits<int>::max();

// ---------------------------------------------------------------------------
// Simplified rank graph (dagre util.simplify): parallel directed edges merge.
// ---------------------------------------------------------------------------

struct REdge {
    int from = -1;
    int to = -1;
    int weight = 1;
    int minlen = 1;
};

struct RG {
    int n = 0;
    std::vector<REdge> edges;
    std::vector<std::vector<int>> in;   // by node: edge indices (insertion order)
    std::vector<std::vector<int>> out;  // by node: edge indices (insertion order)

    RG(const LayoutGraph& g) : n(static_cast<int>(g.nodes.size())),
                               in(static_cast<size_t>(n)), out(static_cast<size_t>(n)) {
        std::map<std::pair<int, int>, int> by_pair;  // (from,to) -> edge idx
        for (const auto& e : g.edges) {
            if (e.from < 0 || e.to < 0 || e.from >= n || e.to >= n) continue;
            auto key = std::make_pair(e.from, e.to);
            auto it = by_pair.find(key);
            if (it != by_pair.end()) {
                REdge& m = edges[static_cast<size_t>(it->second)];
                m.weight += e.weight > 0 ? e.weight : 1;
                m.minlen = std::max(m.minlen, e.minlen);
            } else {
                REdge ne;
                ne.from = e.from;
                ne.to = e.to;
                ne.weight = e.weight > 0 ? e.weight : 1;
                ne.minlen = e.minlen;
                by_pair[key] = static_cast<int>(edges.size());
                edges.push_back(ne);
            }
        }
        for (size_t i = 0; i < edges.size(); ++i) {
            out[static_cast<size_t>(edges[i].from)].push_back(static_cast<int>(i));
            in[static_cast<size_t>(edges[i].to)].push_back(static_cast<int>(i));
        }
    }

    std::vector<int> NodeEdges(int v) const {
        std::vector<int> res = in[static_cast<size_t>(v)];
        res.insert(res.end(), out[static_cast<size_t>(v)].begin(),
                   out[static_cast<size_t>(v)].end());
        return res;
    }

    long long Slack(int ei, const std::vector<int>& rank) const {
        const REdge& e = edges[static_cast<size_t>(ei)];
        return static_cast<long long>(rank[static_cast<size_t>(e.to)]) -
               rank[static_cast<size_t>(e.from)] - e.minlen;
    }
};

// ---------------------------------------------------------------------------
// Undirected tree with graphlib iteration semantics.
// ---------------------------------------------------------------------------

struct TreeLabel {
    int low = 0;
    int lim = 0;
    int parent = -1;  // -1 = no parent (tree root)
};

class Tree {
public:
    std::vector<int> nodes;                       // insertion order
    std::unordered_map<int, int> node_seen;       // id set (any value)
    std::vector<std::pair<int, int>> edges;       // normalised (lo <= hi), insertion order
    std::vector<long long> cut;                   // cut value per edge slot
    std::vector<bool> alive;                      // tombstones keep slots stable
    std::map<std::pair<int, int>, int> index;     // pair -> live edge slot
    std::vector<std::vector<int>> incident;       // by node id: live edge slots

    explicit Tree(int n) : alive(), incident(static_cast<size_t>(n)) {}

    bool has_node(int v) const { return node_seen.count(v) > 0; }

    void add_node(int v) {
        if (has_node(v)) return;
        node_seen[v] = 1;
        nodes.push_back(v);
    }

    bool has_edge(int a, int b) const {
        int lo = std::min(a, b), hi = std::max(a, b);
        return index.count({lo, hi}) > 0;
    }

    void add_edge(int a, int b) {
        int lo = std::min(a, b), hi = std::max(a, b);
        if (index.count({lo, hi})) return;
        int slot = static_cast<int>(edges.size());
        edges.emplace_back(lo, hi);
        cut.push_back(0);
        alive.push_back(true);
        index[{lo, hi}] = slot;
        incident[static_cast<size_t>(lo)].push_back(slot);
        incident[static_cast<size_t>(hi)].push_back(slot);
    }

    void remove_edge(int a, int b) {
        int lo = std::min(a, b), hi = std::max(a, b);
        auto it = index.find({lo, hi});
        if (it == index.end()) return;
        int slot = it->second;
        alive[static_cast<size_t>(slot)] = false;
        auto erase_from = [&](int v) {
            auto& vec = incident[static_cast<size_t>(v)];
            vec.erase(std::remove(vec.begin(), vec.end(), slot), vec.end());
        };
        erase_from(lo);
        erase_from(hi);
        index.erase(it);
    }

    int edge_slot(int a, int b) const {
        int lo = std::min(a, b), hi = std::max(a, b);
        auto it = index.find({lo, hi});
        return it == index.end() ? -1 : it->second;
    }

    // graphlib neighbors(v): union(preds, sucs) with preds first.
    std::vector<int> neighbors(int v) const {
        std::vector<int> out;
        std::unordered_set<int> seen;
        for (int slot : incident[static_cast<size_t>(v)]) {
            const auto& e = edges[static_cast<size_t>(slot)];
            if (e.second == v && seen.insert(e.first).second) out.push_back(e.first);
        }
        for (int slot : incident[static_cast<size_t>(v)]) {
            const auto& e = edges[static_cast<size_t>(slot)];
            if (e.first == v && seen.insert(e.second).second) out.push_back(e.second);
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// longestPath + tighten (kept from the previously green ranker).
// ---------------------------------------------------------------------------

void LongestPath(LayoutGraph& g, std::vector<int>& rank) {
    const int n = static_cast<int>(g.nodes.size());
    RG rg(g);
    std::vector<int> indeg(static_cast<size_t>(n), 0);
    for (const auto& e : rg.edges) ++indeg[static_cast<size_t>(e.to)];
    std::vector<int> pending = indeg;
    std::vector<int> queue;
    for (int i = 0; i < n; ++i)
        if (pending[static_cast<size_t>(i)] == 0) queue.push_back(i);
    std::vector<int> topo;
    topo.reserve(static_cast<size_t>(n));
    for (size_t head = 0; head < queue.size(); ++head) {
        int u = queue[head];
        topo.push_back(u);
        for (int ei : rg.out[static_cast<size_t>(u)]) {
            int v = rg.edges[static_cast<size_t>(ei)].to;
            if (--pending[static_cast<size_t>(v)] == 0) queue.push_back(v);
        }
    }
    for (int u : topo) {
        int r = 0;
        for (int ei : rg.in[static_cast<size_t>(u)]) {
            const REdge& e = rg.edges[static_cast<size_t>(ei)];
            r = std::max(r, rank[static_cast<size_t>(e.from)] + e.minlen);
        }
        rank[static_cast<size_t>(u)] = r;
    }
}

// ---------------------------------------------------------------------------
// feasibleTree (rank/feasible-tree.js).
// ---------------------------------------------------------------------------

int TightTree(Tree& t, const RG& rg, int n, const std::vector<int>& rank) {
    std::vector<int> snapshot = t.nodes;  // _.forEach over t.nodes() snapshot
    std::function<void(int)> dfs = [&](int v) {
        for (int ei : rg.NodeEdges(v)) {
            const REdge& e = rg.edges[static_cast<size_t>(ei)];
            int w = (v == e.from) ? e.to : e.from;
            if (t.has_node(w)) continue;
            if (rg.Slack(ei, rank) != 0) continue;
            t.add_node(w);
            t.add_edge(v, w);
            dfs(w);
        }
    };
    for (int v : snapshot) dfs(v);
    return static_cast<int>(t.nodes.size());
}

int FindMinSlackEdge(const Tree& t, const RG& rg, const std::vector<int>& rank) {
    int best = -1;
    long long best_slack = 0;
    for (size_t i = 0; i < rg.edges.size(); ++i) {
        const REdge& e = rg.edges[i];
        bool v_in = t.has_node(e.from);
        bool w_in = t.has_node(e.to);
        if (v_in == w_in) continue;
        long long s = static_cast<long long>(rank[static_cast<size_t>(e.to)]) - rank[static_cast<size_t>(e.from)] - e.minlen;
        if (best < 0 || s < best_slack) {
            best = static_cast<int>(i);
            best_slack = s;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// low/lim numbering (network-simplex.js initLowLimValues).
// ---------------------------------------------------------------------------

void InitLowLim(Tree& t, std::vector<TreeLabel>& lab) {
    if (t.nodes.empty()) return;
    struct Frame {
        int node;
        size_t child_index;
        int low;
    };
    std::vector<Frame> frames;
    std::unordered_set<int> visited;
    int next_lim = 1;
    int root = t.nodes[0];
    frames.push_back({root, 0, next_lim});
    visited.insert(root);
    while (!frames.empty()) {
        Frame& top = frames.back();
        std::vector<int> neigh = t.neighbors(top.node);
        if (top.child_index < neigh.size()) {
            int w = neigh[top.child_index++];
            if (!visited.count(w)) {
                visited.insert(w);
                frames.push_back({w, 0, next_lim});
            }
            continue;
        }
        lab[static_cast<size_t>(top.node)].low = top.low;
        lab[static_cast<size_t>(top.node)].lim = next_lim++;
        int finished = top.node;
        frames.pop_back();
        if (!frames.empty()) {
            lab[static_cast<size_t>(finished)].parent = frames.back().node;
        }
    }
}

// ---------------------------------------------------------------------------
// cut values (network-simplex.js initCutValues / calcCutValue).
// ---------------------------------------------------------------------------

void InitCutValues(Tree& t, const RG& rg, const std::vector<TreeLabel>& lab,
                   const std::vector<int>& rank) {
    for (auto& c : t.cut) c = 0;
    // postorder(t, t.nodes()) via neighbors; drop the last (root).
    std::vector<int> order;
    std::unordered_set<int> visited;
    std::function<void(int)> dfs = [&](int v) {
        visited.insert(v);
        for (int w : t.neighbors(v)) {
            if (!visited.count(w)) dfs(w);
        }
        order.push_back(v);
    };
    for (int v : t.nodes) {
        if (!visited.count(v)) dfs(v);
    }
    if (order.empty()) return;
    order.pop_back();  // slice(0, len-1): drop the root

    for (int child : order) {
        int parent = lab[static_cast<size_t>(child)].parent;
        if (parent < 0) continue;
        // Orientation of the graph edge between child and parent.
        bool child_is_tail = true;
        long long graph_weight = 0;
        auto find_edge = [&](int from, int to) -> const REdge* {
            for (int ei : rg.out[static_cast<size_t>(from)]) {
                if (rg.edges[static_cast<size_t>(ei)].to == to)
                    return &rg.edges[static_cast<size_t>(ei)];
            }
            return nullptr;
        };
        const REdge* ge = find_edge(child, parent);
        if (!ge) {
            child_is_tail = false;
            ge = find_edge(parent, child);
        }
        if (!ge) continue;  // unreachable for a spanning tree built from rg
        graph_weight = ge->weight;
        long long cut_value = graph_weight;

        for (int ei : rg.NodeEdges(child)) {
            const REdge& e = rg.edges[static_cast<size_t>(ei)];
            bool is_out_edge = (e.from == child);
            int other = is_out_edge ? e.to : e.from;
            if (other == parent) continue;
            bool points_to_head = (is_out_edge == child_is_tail);
            cut_value += points_to_head ? e.weight : -e.weight;
            int slot = t.edge_slot(child, other);
            if (slot >= 0) {
                long long other_cut = t.cut[static_cast<size_t>(slot)];
                cut_value += points_to_head ? -other_cut : other_cut;
            }
        }
        int slot = t.edge_slot(child, parent);
        if (slot >= 0) t.cut[static_cast<size_t>(slot)] = cut_value;
    }
}

// ---------------------------------------------------------------------------
// simplex loop (leaveEdge / enterEdge / exchangeEdges / updateRanks).
// ---------------------------------------------------------------------------

int LeaveEdge(const Tree& t) {
    for (size_t i = 0; i < t.edges.size(); ++i) {
        if (!t.alive[i]) continue;
        if (t.cut[i] < 0) return static_cast<int>(i);
    }
    return -1;
}

int EnterEdge(const Tree& t, const RG& rg, const std::vector<TreeLabel>& lab,
              const std::vector<int>& rank, int leave_slot, bool* out_flipped) {
    // v/w per the tree edge's normalised orientation.
    int v = t.edges[static_cast<size_t>(leave_slot)].first;
    int w = t.edges[static_cast<size_t>(leave_slot)].second;
    // Redirect to the rank graph's orientation when needed.
    bool have_direct = false;
    for (int ei : rg.out[static_cast<size_t>(v)]) {
        if (rg.edges[static_cast<size_t>(ei)].to == w) { have_direct = true; break; }
    }
    if (!have_direct) std::swap(v, w);

    const TreeLabel& v_lab = lab[static_cast<size_t>(v)];
    const TreeLabel& w_lab = lab[static_cast<size_t>(w)];
    const TreeLabel* tail_label = &v_lab;
    bool flip = false;
    if (v_lab.lim > w_lab.lim) {
        tail_label = &w_lab;
        flip = true;
    }
    auto is_descendant = [&](int node) {
        const TreeLabel& nl = lab[static_cast<size_t>(node)];
        return tail_label->low <= nl.lim && nl.lim <= tail_label->lim;
    };

    int best = -1;
    long long best_slack = 0;
    for (size_t i = 0; i < rg.edges.size(); ++i) {
        const REdge& e = rg.edges[i];
        bool desc_v = is_descendant(e.from);
        bool desc_w = is_descendant(e.to);
        if (flip == desc_v && flip != desc_w) {
            long long s = static_cast<long long>(rank[static_cast<size_t>(e.to)]) - rank[static_cast<size_t>(e.from)] - e.minlen;
            if (best < 0 || s < best_slack) {
                best = static_cast<int>(i);
                best_slack = s;
            }
        }
    }
    if (out_flipped) *out_flipped = flip;
    return best;
}

void UpdateRanks(const Tree& t, const RG& rg, const std::vector<TreeLabel>& lab,
                 std::vector<int>& rank) {
    int root = -1;
    for (int v : t.nodes) {
        if (lab[static_cast<size_t>(v)].parent < 0) { root = v; break; }
    }
    if (root < 0) return;
    std::vector<int> acc;  // preorder
    std::unordered_set<int> visited;
    std::function<void(int)> dfs = [&](int v) {
        if (visited.count(v)) return;
        visited.insert(v);
        acc.push_back(v);
        for (int w : t.neighbors(v)) dfs(w);
    };
    dfs(root);

    auto edge_minlen = [&](int from, int to, bool* found) {
        *found = false;
        int minlen = 0;
        for (int ei : rg.out[static_cast<size_t>(from)]) {
            if (rg.edges[static_cast<size_t>(ei)].to == to) {
                minlen = rg.edges[static_cast<size_t>(ei)].minlen;
                *found = true;
                return minlen;
            }
        }
        return minlen;
    };

    for (size_t i = 1; i < acc.size(); ++i) {
        int v = acc[i];
        int parent = lab[static_cast<size_t>(v)].parent;
        if (parent < 0) continue;
        bool found = false;
        int minlen = edge_minlen(v, parent, &found);  // g.edge(v, parent)
        if (!found) {
            minlen = edge_minlen(parent, v, &found);  // flipped
            rank[static_cast<size_t>(v)] =
                rank[static_cast<size_t>(parent)] + (found ? minlen : 0);
        } else {
            rank[static_cast<size_t>(v)] = rank[static_cast<size_t>(parent)] - minlen;
        }
    }
}

}  // namespace

void AssignRanks(LayoutGraph& g) {
    const int n = static_cast<int>(g.nodes.size());
    if (n == 0) return;
    for (auto& nd : g.nodes) nd.rank = 0;

    // Simplified rank graph (parallel directed edges merged).
    RG rg(g);
    std::vector<int> rank(static_cast<size_t>(n), 0);

    // 1. longest path init.
    LongestPath(g, rank);

    // 2. feasible tight tree.
    Tree t(n);
    t.add_node(g.nodes[0].id);
    while (static_cast<int>(t.nodes.size()) < n) {
        TightTree(t, rg, n, rank);
        if (static_cast<int>(t.nodes.size()) >= n) break;
        int eidx = FindMinSlackEdge(t, rg, rank);
        if (eidx < 0) break;  // disconnected; dagre would assert
        const REdge& e = rg.edges[static_cast<size_t>(eidx)];
        long long s = static_cast<long long>(rank[static_cast<size_t>(e.to)]) - rank[static_cast<size_t>(e.from)] - e.minlen;
        long long delta = t.has_node(e.from) ? s : -s;
        for (int v : t.nodes) rank[static_cast<size_t>(v)] += static_cast<int>(delta);
    }

    // 3. low/lim + cut values.
    std::vector<TreeLabel> lab(static_cast<size_t>(n));
    InitLowLim(t, lab);
    InitCutValues(t, rg, lab, rank);

    // 4. simplex iterations.
    while (true) {
        int leave_slot = LeaveEdge(t);
        if (leave_slot < 0) break;
        int fidx = EnterEdge(t, rg, lab, rank, leave_slot, nullptr);
        if (fidx < 0) break;  // safety; dagre would crash on undefined too
        int ev = t.edges[static_cast<size_t>(leave_slot)].first;
        int ew = t.edges[static_cast<size_t>(leave_slot)].second;
        const REdge& f = rg.edges[static_cast<size_t>(fidx)];
        t.remove_edge(ev, ew);
        t.add_edge(f.from, f.to);  // normalises orientation like t.setEdge(f.v, f.w)
        InitLowLim(t, lab);
        InitCutValues(t, rg, lab, rank);
        UpdateRanks(t, rg, lab, rank);
    }

    // Publish ranks; shift so the minimum rank is 0 (dagre's normalizeRanks).
    int min_rank = kIntMax;
    for (auto& nd : g.nodes) {
        nd.rank = rank[static_cast<size_t>(nd.id)];
        min_rank = std::min(min_rank, nd.rank);
    }
    if (min_rank != kIntMax && min_rank != 0) {
        for (auto& nd : g.nodes) nd.rank -= min_rank;
    }
}

}  // namespace mermaid
