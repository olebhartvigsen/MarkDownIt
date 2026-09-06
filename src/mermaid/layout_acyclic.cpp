// Greedy feedback-arc-set (Eades-Lin-Smyth) cycle breaking.
#include "layout_internal.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace mermaid {

namespace {

// DFS 3-color cycle detection.
bool HasCycleDFS(const std::vector<std::vector<int>>& adj, int n) {
    enum Color { White = 0, Gray = 1, Black = 2 };
    std::vector<Color> color(n, White);
    // Iterative DFS to avoid deep recursion.
    for (int start = 0; start < n; ++start) {
        if (color[start] != White) continue;
        std::vector<std::pair<int,size_t>> stack;
        stack.push_back({start, 0});
        color[start] = Gray;
        while (!stack.empty()) {
            auto& top = stack.back();
            int u = top.first;
            size_t& i = top.second;
            if (i < adj[u].size()) {
                int v = adj[u][i++];
                if (color[v] == Gray) return true;
                if (color[v] == White) {
                    color[v] = Gray;
                    stack.push_back({v, 0});
                }
            } else {
                color[u] = Black;
                stack.pop_back();
            }
        }
    }
    return false;
}

}  // namespace

bool IsAcyclic(const LayoutGraph& g) {
    int n = static_cast<int>(g.nodes.size());
    std::vector<std::vector<int>> adj(n);
    for (const auto& e : g.edges) {
        if (e.from == e.to) continue;  // ignore self-loops
        if (e.from >= 0 && e.from < n && e.to >= 0 && e.to < n) {
            adj[e.from].push_back(e.to);
        }
    }
    return !HasCycleDFS(adj, n);
}

std::vector<int> MakeAcyclic(LayoutGraph& g) {
    std::vector<int> reversed_indices;

    // Step 1: extract self-loops.
    std::vector<LayoutEdge> kept;
    kept.reserve(g.edges.size());
    for (size_t i = 0; i < g.edges.size(); ++i) {
        const auto& e = g.edges[i];
        if (e.from == e.to) {
            g.self_loops.push_back({e.from, static_cast<int>(i)});
            if (e.from >= 0 && e.from < static_cast<int>(g.nodes.size())) {
                g.nodes[e.from].is_self_loop_host = true;
            }
        } else {
            kept.push_back(e);
        }
    }
    g.edges = std::move(kept);

    int n = static_cast<int>(g.nodes.size());
    if (n == 0 || g.edges.empty()) return reversed_indices;

    // Build working degree structures.
    std::vector<std::vector<int>> out_adj(n), in_adj(n);
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n) continue;
        out_adj[e.from].push_back(e.to);
        in_adj[e.to].push_back(e.from);
    }
    std::vector<int> out_deg(n), in_deg(n);
    for (int i = 0; i < n; ++i) {
        out_deg[i] = static_cast<int>(out_adj[i].size());
        in_deg[i] = static_cast<int>(in_adj[i].size());
    }
    std::vector<bool> removed(n, false);

    std::vector<int> left, right;
    left.reserve(n);
    right.reserve(n);

    auto remove_node = [&](int u) {
        removed[u] = true;
        for (int v : out_adj[u]) if (!removed[v]) in_deg[v]--;
        for (int v : in_adj[u]) if (!removed[v]) out_deg[v]--;
    };

    int placed = 0;
    while (placed < n) {
        bool progress = true;
        while (progress) {
            progress = false;
            // Sinks: out_deg == 0.
            for (int u = 0; u < n; ++u) {
                if (!removed[u] && out_deg[u] == 0) {
                    right.push_back(u);
                    remove_node(u);
                    ++placed;
                    progress = true;
                }
            }
            // Sources: in_deg == 0.
            for (int u = 0; u < n; ++u) {
                if (!removed[u] && in_deg[u] == 0) {
                    left.push_back(u);
                    remove_node(u);
                    ++placed;
                    progress = true;
                }
            }
        }
        if (placed >= n) break;
        // Pick node with max (out - in).
        int best = -1, best_score = 0;
        for (int u = 0; u < n; ++u) {
            if (removed[u]) continue;
            int s = out_deg[u] - in_deg[u];
            if (best == -1 || s > best_score) { best = u; best_score = s; }
        }
        if (best == -1) break;
        left.push_back(best);
        remove_node(best);
        ++placed;
    }

    // Build permutation: left + reverse(right).
    std::vector<int> pos(n, -1);
    int idx = 0;
    for (int u : left) pos[u] = idx++;
    for (auto it = right.rbegin(); it != right.rend(); ++it) pos[*it] = idx++;

    // Reverse backward edges.
    for (size_t i = 0; i < g.edges.size(); ++i) {
        auto& e = g.edges[i];
        if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n) continue;
        if (pos[e.from] > pos[e.to]) {
            std::swap(e.from, e.to);
            e.reversed = !e.reversed;
            reversed_indices.push_back(static_cast<int>(i));
        }
    }
    return reversed_indices;
}

}  // namespace mermaid
