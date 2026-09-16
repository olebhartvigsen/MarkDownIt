// DirectWrite-independent measurement seam. The injected callback selects node
// dimensions, then the regular flowchart pipeline handles every direction.
#include "layout.h"
#include "layout_internal.h"

namespace mermaid {
namespace {
constexpr float kWrappingWidth = 200.0f;
constexpr float kPadding = 15.0f;
}  // namespace

LaidOutFlowchart LayoutFlowchartWith(const Flowchart& flow,
                                     MeasureFn measure,
                                     void* ctx) {
    if (!measure) return LayoutFlowchart(flow, LayoutParams{});

    LayoutGraph g = FlowchartToLayoutGraph(flow);
    if (g.nodes.empty()) return LaidOutFlowchart{};
    for (size_t i = 0; i < g.nodes.size() && i < flow.nodes.size(); ++i) {
        const std::string& label = flow.nodes[i].label.empty()
            ? flow.nodes[i].id : flow.nodes[i].label;
        const LabelSize m = measure(label, kWrappingWidth, ctx);
        // A failed text API must retain the known-good deterministic layout,
        // never turn a diagram into a set of padding-only nodes.
        if (m.width <= 0.0f || m.height <= 0.0f)
            return LayoutFlowchart(flow, LayoutParams{});
        g.nodes[i].width = m.width + kPadding * 2.0f;
        g.nodes[i].height = m.height + kPadding * 2.0f;
    }
    return FinalizeFlowchartLayout(flow, std::move(g), LayoutParams{});
}

}  // namespace mermaid
