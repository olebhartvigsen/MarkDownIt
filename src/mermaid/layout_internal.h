#pragma once
// Internal layout graph. Grown by Tasks 5-10; not part of the public API.
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace mermaid {

struct LayoutNode {
    int id = -1;
    std::string label;
    float width = 0;
    float height = 0;
    int rank = -1;
    int order = -1;
    float x = 0, y = 0;
    bool is_dummy = false;
    bool is_self_loop_host = false;
};

struct LayoutEdge {
    int from = -1;
    int to = -1;
    bool reversed = false;
    int minlen = 1;
    int weight = 1;
    std::string label;
};

struct LayoutGraph {
    std::vector<LayoutNode> nodes;
    std::vector<LayoutEdge> edges;
    std::vector<std::pair<int,int>> self_loops;
};

std::vector<int> MakeAcyclic(LayoutGraph& g);
bool IsAcyclic(const LayoutGraph& g);
void AssignRanks(LayoutGraph& g);

inline LayoutGraph MakeGraph(int node_count,
                             std::initializer_list<std::pair<int,int>> edges) {
    LayoutGraph g;
    g.nodes.resize(static_cast<size_t>(node_count));
    for (int i = 0; i < node_count; ++i) g.nodes[i].id = i;
    for (auto pr : edges) {
        LayoutEdge e; e.from = pr.first; e.to = pr.second;
        g.edges.push_back(e);
    }
    return g;
}

}  // namespace mermaid
