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
    int original_edge_index = -1;  // index into original_edges (dummies restored by Denormalize)
};

struct DummyChain {
    int original_edge_index = -1;      // index in the original edges list (pre-normalize)
    std::vector<int> dummy_nodes;      // node ids of dummies in order from u to v
};

struct LayoutGraph {
    std::vector<LayoutNode> nodes;
    std::vector<LayoutEdge> edges;
    std::vector<std::pair<int,int>> self_loops;
    std::vector<DummyChain> dummy_chains;
    std::vector<LayoutEdge> original_edges;   // populated by Normalize; empty before that
};

std::vector<int> MakeAcyclic(LayoutGraph& g);
bool IsAcyclic(const LayoutGraph& g);
void AssignRanks(LayoutGraph& g);
void Normalize(LayoutGraph& g);
void Denormalize(LayoutGraph& g);
bool AllEdgesUnitLength(const LayoutGraph& g);
void Order(LayoutGraph& g);
int CountCrossings(const LayoutGraph& g);

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
