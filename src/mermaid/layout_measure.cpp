// DirectWrite-independent measurement seam. The injected callback selects node
// dimensions, then the regular flowchart pipeline handles every direction.
#include "elk_profile.h"
#include "layout.h"

#include <cmath>
#include "layout_internal.h"

namespace mermaid {
namespace {
constexpr float kWrappingWidth = 200.0f;
constexpr float kPadding = 15.0f;
}  // namespace

LaidOutFlowchart LayoutFlowchartWith(const Flowchart& flow,
                                     MeasureFn measure,
                                     void* ctx) {
    if (!measure) return LayoutFlowchart(flow, LayoutParamsForProfile());

    LayoutGraph g = FlowchartToLayoutGraph(flow);
    if (g.nodes.empty()) return LaidOutFlowchart{};
    for (size_t i = 0; i < g.nodes.size() && i < flow.nodes.size(); ++i) {
        const std::string& label = flow.nodes[i].label.empty()
            ? flow.nodes[i].id : flow.nodes[i].label;
        const LabelSize m = measure(label, kWrappingWidth, ctx);
        // A failed text API must retain the known-good deterministic layout,
        // never turn a diagram into a set of padding-only nodes.
        if (m.width <= 0.0f || m.height <= 0.0f)
            return LayoutFlowchart(flow, LayoutParamsForProfile());
        // Whole points, because the two ports measure the same label
        // with different engines: DirectWrite and Core Text agree on a
        // label but not on its last hundredth. A diagram is a block in
        // the document, so a fraction of a point per node becomes a
        // different page height on each platform, and every scroll
        // position after it. Snapping the measure once, here, keeps the
        // diagram the same size on both.
        g.nodes[i].width = std::ceil(m.width) + kPadding * 2.0f;
        g.nodes[i].height = std::ceil(m.height) + kPadding * 2.0f;
    }
    return FinalizeFlowchartLayout(flow, std::move(g), LayoutParamsForProfile());
}

}  // namespace mermaid
