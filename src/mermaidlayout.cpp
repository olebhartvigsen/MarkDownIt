#include "mermaidlayout.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <queue>

namespace mermaid {

// --- Constants (DIPs) ---

static constexpr float kNodePadX     = 12.0f;   // inner horizontal padding
static constexpr float kNodePadY     = 8.0f;    // inner vertical padding
static constexpr float kNodeMinW     = 40.0f;   // minimum node width
static constexpr float kLayerGap     = 50.0f;   // gap between layers
static constexpr float kColGap       = 30.0f;   // gap between columns in a layer
static constexpr float kSeqHeaderH   = 40.0f;   // sequence header height
static constexpr float kSeqRowH      = 32.0f;   // sequence message row height
static constexpr float kSeqColGap    = 80.0f;   // sequence column gap
static constexpr float kFontSize     = 14.0f;   // diagram font height

// --- Helpers ---

static float MeasureText(const std::string& text, MeasureFn measure, void* ctx) {
    if (measure) return measure(text, ctx);
    return static_cast<float>(text.size()) * 8.0f;  // fallback stub
}

static void ComputeNodeSize(const GraphNode& gn, MeasureFn measure, void* mctx,
                             float& w, float& h) {
    float textW = MeasureText(gn.label, measure, mctx);
    w = std::max(kNodeMinW, textW + 2.0f * kNodePadX);
    h = kFontSize + 2.0f * kNodePadY;
    // Diamond and circle need extra room
    if (gn.shape == NodeShape::Diamond) {
        w += 16.0f;
        h += 12.0f;
    } else if (gn.shape == NodeShape::Circle) {
        float d = std::max(w, h);
        w = d;
        h = d;
    } else if (gn.shape == NodeShape::Stadium) {
        h += 6.0f;
    }
}

// --- Flowchart layout (simplified Sugiyama) ---

// Assign layers using longest path from roots (no in-edges).
// Cycles broken by visited-set.
static void AssignLayers(const Diagram& d, std::vector<int>& layer,
                          int& maxLayer) {
    int n = static_cast<int>(d.nodes.size());
    layer.assign(n, 0);
    maxLayer = 0;

    // Build adjacency
    std::vector<std::vector<int>> succ(n), pred(n);
    std::vector<int> inDeg(n, 0);
    for (const auto& e : d.edges) {
        if (e.from >= 0 && e.from < n && e.to >= 0 && e.to < n) {
            succ[e.from].push_back(e.to);
            pred[e.to].push_back(e.from);
            inDeg[e.from]++;  // we'll use this differently
        }
    }

    // Recompute in-degree (incoming edges)
    std::fill(inDeg.begin(), inDeg.end(), 0);
    for (const auto& e : d.edges) {
        if (e.to >= 0 && e.to < n) inDeg[e.to]++;
    }

    // BFS from roots (inDeg == 0), assign layer = max(pred layers) + 1
    // For cycles: nodes with inDeg > 0 that never get visited get layer 0
    std::queue<int> q;
    for (int i = 0; i < n; i++) {
        if (inDeg[i] == 0) {
            layer[i] = 0;
            q.push(i);
        }
    }

    // If no roots (all cyclic), start from node 0
    if (q.empty() && n > 0) {
        layer[0] = 0;
        q.push(0);
        // Remove one edge from each cyclic node to break the cycle
        // by setting inDeg to 0 for the starting node
        // (effectively ignoring back-edges to node 0)
    }

    std::vector<int> remainingInDeg = inDeg;
    // For the all-cyclic case, treat node 0 as root
    if (n > 0 && inDeg[0] > 0 && q.size() == 1) {
        remainingInDeg[0] = 0;
    }

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (int v : succ[u]) {
            if (v == u) continue;  // self-loop
            layer[v] = std::max(layer[v], layer[u] + 1);
            maxLayer = std::max(maxLayer, layer[v]);
            if (--remainingInDeg[v] == 0) {
                q.push(v);
            }
        }
    }

    // Any nodes not yet assigned (part of cycles) get layer 0
    for (int i = 0; i < n; i++) {
        if (remainingInDeg[i] > 0) {
            layer[i] = std::max(0, layer[i]);
            maxLayer = std::max(maxLayer, layer[i]);
        }
    }
}

static void LayoutFlowchart(const Diagram& d, Layout& layout,
                             MeasureFn measure, void* mctx, float maxWidth) {
    int n = static_cast<int>(d.nodes.size());
    if (n == 0) return;

    // 1. Assign layers
    std::vector<int> layer;
    int maxLayer = 0;
    AssignLayers(d, layer, maxLayer);

    // 2. Group nodes by layer
    std::vector<std::vector<int>> layersByNode(maxLayer + 1);
    for (int i = 0; i < n; i++) {
        layersByNode[layer[i]].push_back(i);
    }

    // 3. Compute node sizes
    layout.nodes.resize(n);
    for (int i = 0; i < n; i++) {
        layout.nodes[i].label = d.nodes[i].label;
        layout.nodes[i].shape = d.nodes[i].shape;
        ComputeNodeSize(d.nodes[i], measure, mctx,
                         layout.nodes[i].w, layout.nodes[i].h);
    }

    // 4. Position layers along main axis, center each layer on cross axis
    bool horizontal = (d.dir == Direction::LR || d.dir == Direction::RL);

    float mainAxis = 0;
    for (int li = 0; li <= maxLayer; li++) {
        auto& layerNodes = layersByNode[li];
        int count = static_cast<int>(layerNodes.size());
        if (count == 0) continue;

        // Total cross-axis span for this layer
        float crossSpan = 0;
        for (int idx : layerNodes) {
            if (horizontal)
                crossSpan += layout.nodes[idx].h;
            else
                crossSpan += layout.nodes[idx].w;
            crossSpan += kColGap;
        }
        crossSpan -= kColGap;  // remove trailing gap

        float crossStart = 0;  // centered later

        for (int j = 0; j < count; j++) {
            int ni = layerNodes[j];
            float& mainPos = horizontal ? layout.nodes[ni].x : layout.nodes[ni].y;
            float& crossPos = horizontal ? layout.nodes[ni].y : layout.nodes[ni].x;
            float crossSize = horizontal ? layout.nodes[ni].h : layout.nodes[ni].w;

            mainPos = mainAxis;
            crossPos = crossStart;
            crossStart += crossSize + kColGap;
        }

        // Advance main axis by max node size in this layer
        float layerThickness = 0;
        for (int idx : layerNodes) {
            float sz = horizontal ? layout.nodes[idx].w : layout.nodes[idx].h;
            layerThickness = std::max(layerThickness, sz);
        }
        mainAxis += layerThickness + kLayerGap;
    }

    // 5. Center each layer relative to the widest layer
    // Find max cross-axis span
    float maxCrossSpan = 0;
    for (int li = 0; li <= maxLayer; li++) {
        auto& ln = layersByNode[li];
        if (ln.empty()) continue;
        float span = 0;
        for (int idx : ln) {
            span += (horizontal ? layout.nodes[idx].h : layout.nodes[idx].w) + kColGap;
        }
        span -= kColGap;
        maxCrossSpan = std::max(maxCrossSpan, span);
    }

    // Shift each layer to center
    for (int li = 0; li <= maxLayer; li++) {
        auto& ln = layersByNode[li];
        if (ln.empty()) continue;
        float span = 0;
        for (int idx : ln) {
            span += (horizontal ? layout.nodes[idx].h : layout.nodes[idx].w) + kColGap;
        }
        span -= kColGap;
        float offset = (maxCrossSpan - span) / 2.0f;
        for (int idx : ln) {
            if (horizontal)
                layout.nodes[idx].y += offset;
            else
                layout.nodes[idx].x += offset;
        }
    }

    // 6. Compute total diagram size
    float totalMain = mainAxis;
    float totalCross = maxCrossSpan;
    // Account for RL/BT by repositioning from the end
    if (d.dir == Direction::RL || d.dir == Direction::BT) {
        // Mirror main axis positions
        for (int i = 0; i < n; i++) {
            if (d.dir == Direction::RL)
                layout.nodes[i].x = totalMain - layout.nodes[i].x - layout.nodes[i].w;
            else
                layout.nodes[i].y = totalMain - layout.nodes[i].y - layout.nodes[i].h;
        }
    }

    if (horizontal) {
        layout.width = totalMain;
        layout.height = totalCross;
    } else {
        layout.width = totalCross;
        layout.height = totalMain;
    }

    // 7. Lay out edges as polylines
    layout.edges.reserve(d.edges.size());
    for (const auto& e : d.edges) {
        if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n) continue;
        LaidOutEdge le;
        le.label = e.label;
        le.style = e.style;
        le.head = e.head;

        const auto& na = layout.nodes[e.from];
        const auto& nb = layout.nodes[e.to];

        // Connect center-to-center with a bend at the midpoint
        float ax = na.x + na.w / 2.0f;
        float ay = na.y + na.h / 2.0f;
        float bx = nb.x + nb.w / 2.0f;
        float by = nb.y + nb.h / 2.0f;

        if (horizontal) {
            // Edge goes left-to-right (or right-to-left for RL)
            float startX = (d.dir == Direction::RL) ? na.x : na.x + na.w;
            float endX = (d.dir == Direction::RL) ? nb.x + nb.w : nb.x;
            float midY1 = ay, midY2 = by;
            le.points.push_back({startX, midY1});
            if (std::abs(midY1 - midY2) > 1.0f) {
                float midX = (startX + endX) / 2.0f;
                le.points.push_back({midX, midY1});
                le.points.push_back({midX, midY2});
            }
            le.points.push_back({endX, midY2});
        } else {
            // Vertical (TD/BT)
            float startY = (d.dir == Direction::BT) ? na.y : na.y + na.h;
            float endY = (d.dir == Direction::BT) ? nb.y + nb.h : nb.y;
            float midX1 = ax, midX2 = bx;
            le.points.push_back({midX1, startY});
            if (std::abs(midX1 - midX2) > 1.0f) {
                float midY = (startY + endY) / 2.0f;
                le.points.push_back({midX1, midY});
                le.points.push_back({midX2, midY});
            }
            le.points.push_back({midX2, endY});
        }

        // Label at midpoint
        if (!e.label.empty()) {
            size_t mid = le.points.size() / 2;
            le.labelX = le.points[mid].first;
            le.labelY = le.points[mid].second - kFontSize - 2.0f;
        }

        layout.edges.push_back(std::move(le));
    }
}

// --- Sequence diagram layout ---

static void LayoutSequence(const Diagram& d, Layout& layout,
                            MeasureFn measure, void* mctx, float maxWidth) {
    int n = static_cast<int>(d.nodes.size());
    if (n == 0) return;

    layout.nodes.resize(n);

    // Compute column positions
    float x = 0;
    for (int i = 0; i < n; i++) {
        layout.nodes[i].label = d.nodes[i].label;
        layout.nodes[i].shape = NodeShape::Rect;
        float textW = MeasureText(d.nodes[i].label, measure, mctx);
        layout.nodes[i].w = std::max(kNodeMinW, textW + 2.0f * kNodePadX);
        layout.nodes[i].h = kSeqHeaderH;
        layout.nodes[i].x = x;
        layout.nodes[i].y = 0;
        x += layout.nodes[i].w + kSeqColGap;
    }
    x -= kSeqColGap;  // remove trailing gap

    layout.width = x;

    // Place messages as horizontal edges
    layout.edges.reserve(d.messages.size());
    float msgY = kSeqHeaderH + 10.0f;
    for (const auto& msg : d.messages) {
        if (msg.from < 0 || msg.from >= n || msg.to < 0 || msg.to >= n) continue;

        LaidOutEdge le;
        le.label = msg.label;
        le.style = msg.style;
        le.head = ArrowHead::Arrow;

        float fromX = layout.nodes[msg.from].x + layout.nodes[msg.from].w / 2.0f;
        float toX   = layout.nodes[msg.to].x + layout.nodes[msg.to].w / 2.0f;
        float y = msgY;

        le.points.push_back({fromX, y});
        le.points.push_back({toX, y});

        le.labelX = (fromX + toX) / 2.0f;
        le.labelY = y - kFontSize - 2.0f;

        layout.edges.push_back(std::move(le));
        msgY += kSeqRowH;
    }

    layout.height = msgY + 10.0f;
    if (layout.height < kSeqHeaderH + 20.0f) {
        layout.height = kSeqHeaderH + 20.0f;
    }
}

// --- Main entry point ---

Layout ComputeLayout(const Diagram& d, float maxWidth,
                     MeasureFn measure, void* measureCtx) {
    Layout layout;

    if (d.type == DiagramType::Flowchart) {
        LayoutFlowchart(d, layout, measure, measureCtx, maxWidth);
    } else if (d.type == DiagramType::Sequence) {
        LayoutSequence(d, layout, measure, measureCtx, maxWidth);
    }

    // Clamp to maxWidth
    if (maxWidth > 0 && layout.width > maxWidth) {
        layout.width = maxWidth;
    }

    return layout;
}

}  // namespace mermaid
