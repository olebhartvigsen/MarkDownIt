#pragma once
// Internal layout graph. Grown by Tasks 5-10; not part of the public API.
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace mermaid {

enum class NodeShape { Rect, Round, Stadium, Diamond, Circle };

// Edge presentation. Shared by the parse model (model.h) and the internal
// layout graph so routed edges can carry their style to the renderer.
enum class LineStyle { Solid, Dotted, Thick };
enum class Head { None, Arrow };

struct Point {
    double x = 0;
    double y = 0;
};

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
    NodeShape shape = NodeShape::Rect;
};

struct LayoutEdge {
    int from = -1;
    int to = -1;
    bool reversed = false;
    int minlen = 1;
    int weight = 1;
    std::string label;
    int original_edge_index = -1;  // index into original_edges (dummies restored by Denormalize)
    std::vector<Point> route;      // populated by RouteEdges (Task 10)
    // Edge-label layout (class diagrams): when nonzero, dagre's
    // injectEdgeLabelProxies puts a label-proxy dummy of this size on the
    // middle rank of the edge; its laid-out position becomes the label
    // anchor (fixupEdgeLabelCoords). C++ port stores the proxy's final
    // center here after Denormalize.
    float label_width = 0;
    float label_height = 0;
    int label_proxy_node = -1;     // dummy node id of the label proxy
    Point label_pos;               // final label center (after margin shift)
    // Renderer-facing edge styling (propagated from FlowEdge in layout.cpp;
    // denormalized edges carry the original's style forward).
    LineStyle style = LineStyle::Solid;
    Head head = Head::Arrow;
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

struct LayoutParams {
    double rank_sep = 50.0;
    double node_sep = 50.0;
    double edge_sep = 10.0;
    double margin = 20.0;
    // Flowchart reproduces mermaid with real node widths through BK plus a
    // left-align of real nodes' left edges to 0. Diagram types that match
    // the layout of a *post-translateGraph* dagre graph (state) must keep
    // the raw BK coordinates instead.
    bool left_align_zero = true;
};

void AssignCoordinates(LayoutGraph& g, const LayoutParams& p);
void RouteEdges(LayoutGraph& g, const LayoutParams& p);

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
