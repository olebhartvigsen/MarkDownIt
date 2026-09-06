// Task 11: DirectWrite measurement seam. LayoutFlowchartWith mirrors
// LayoutFlowchart but overrides node width/height using the injected
// MeasureFn (wrappingWidth=200, padding=15 on each side, matching mermaid).
// Kept separate from layout.cpp to avoid stepping on the concurrent edits.
#include "layout.h"
#include "layout_internal.h"

namespace mermaid {

namespace {
constexpr float kWrappingWidth = 200.0f;
constexpr float kPadding = 15.0f;
}  // namespace

LaidOutFlowchart LayoutFlowchartWith(const Flowchart& flow,
                                     MeasureFn measure,
                                     void* ctx,
                                     float /*zoom*/) {
    LaidOutFlowchart out;
    LayoutGraph g = FlowchartToLayoutGraph(flow);
    if (g.nodes.empty()) return out;

    // Replace stubbed sizes with real measurement + mermaid padding.
    if (measure) {
        for (size_t i = 0; i < g.nodes.size() && i < flow.nodes.size(); ++i) {
            const std::string& label =
                flow.nodes[i].label.empty() ? flow.nodes[i].id : flow.nodes[i].label;
            LabelSize m = measure(label, kWrappingWidth, ctx);
            g.nodes[i].width  = m.width  + kPadding * 2.0f;
            g.nodes[i].height = m.height + kPadding * 2.0f;
        }
    }

    LayoutParams p;  // defaults; caller uses LayoutFlowchart for custom params.
    MakeAcyclic(g);
    AssignRanks(g);
    Normalize(g);
    Order(g);
    AssignCoordinates(g, p);
    RouteEdges(g, p);
    Denormalize(g);

    double margin = p.margin;
    if (margin > 0.0) {
        for (auto& n : g.nodes) {
            n.x = static_cast<float>(n.x + margin);
            n.y = static_cast<float>(n.y + margin);
        }
        for (auto& e : g.edges) {
            for (auto& pt : e.route) { pt.x += margin; pt.y += margin; }
        }
    }

    double max_x = 0.0, max_y = 0.0;
    for (const auto& n : g.nodes) {
        double right  = static_cast<double>(n.x) + static_cast<double>(n.width)  * 0.5;
        double bottom = static_cast<double>(n.y) + static_cast<double>(n.height) * 0.5;
        if (right  > max_x) max_x = right;
        if (bottom > max_y) max_y = bottom;
    }
    for (const auto& e : g.edges) {
        for (const auto& pt : e.route) {
            if (pt.x > max_x) max_x = pt.x;
            if (pt.y > max_y) max_y = pt.y;
        }
    }
    out.width  = max_x + margin;
    out.height = max_y + margin;
    out.nodes = std::move(g.nodes);
    out.edges = std::move(g.edges);
    out.lanes = ComputeLaneBoxes(flow, out.nodes);
    for (const auto& lb : out.lanes) {
        double r = static_cast<double>(lb.x) + static_cast<double>(lb.width);
        double b = static_cast<double>(lb.y) + static_cast<double>(lb.height);
        if (r + margin > out.width)  out.width  = r + margin;
        if (b + margin > out.height) out.height = b + margin;
    }
    return out;
}

}  // namespace mermaid
