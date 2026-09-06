// Swimlane lane-box computation. Post-layout: for each Subgraph, compute
// axis-aligned bounding box around all contained laid-out nodes and pad
// by kLanePadding on all sides. Nested subgraphs are treated as flat here
// (their nodes are the transitive union). Titles are drawn separately by
// the renderer.
#include "layout.h"
#include "layout_internal.h"
#include "model.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace mermaid {

// Lane padding in DIP, matches mermaid's default subgraph padding (20)
// minus a bit for the fact we measure from node bounding boxes already
// including their own PADDING. Kept as an explicit const so oracle and
// C++ agree.
constexpr float kLanePadding = 20.0f;
// Space reserved above the node band for the title strip.
constexpr float kLaneTitleBand = 20.0f;

static void CollectNodeIndices(const std::vector<Subgraph>& sgs, int idx,
                               std::unordered_set<int>& out) {
    for (int ni : sgs[idx].node_indices) out.insert(ni);
    for (int c : sgs[idx].child_subgraphs) CollectNodeIndices(sgs, c, out);
}

std::vector<LaneBox> ComputeLaneBoxes(const Flowchart& flow,
                                      const std::vector<LayoutNode>& laid) {
    std::vector<LaneBox> out;
    out.reserve(flow.subgraphs.size());
    for (size_t si = 0; si < flow.subgraphs.size(); ++si) {
        const auto& sg = flow.subgraphs[si];
        std::unordered_set<int> idxs;
        CollectNodeIndices(flow.subgraphs, (int)si, idxs);
        if (idxs.empty()) continue;

        float minx = std::numeric_limits<float>::infinity();
        float miny = std::numeric_limits<float>::infinity();
        float maxx = -std::numeric_limits<float>::infinity();
        float maxy = -std::numeric_limits<float>::infinity();
        bool any = false;
        for (const auto& ln : laid) {
            if (ln.is_dummy) continue;
            // Match by label (nodes carry the id in .label when no bracket
            // label was set, else the visible label). We rebuild indices by
            // matching flow.nodes[i].id/label against ln.label; simpler is
            // to match by array position after filtering dummies.
            (void)ln;
        }
        // Match by ordinal: LaidOutFlowchart preserves the same order of
        // real nodes as flow.nodes (dummies were removed by Denormalize).
        // Build a real-node index map.
        std::vector<int> real_ord; real_ord.reserve(laid.size());
        for (size_t i = 0; i < laid.size(); ++i) {
            if (!laid[i].is_dummy) real_ord.push_back((int)i);
        }
        for (int flowIdx : idxs) {
            if (flowIdx < 0 || flowIdx >= (int)real_ord.size()) continue;
            const auto& ln = laid[real_ord[flowIdx]];
            float l = ln.x - ln.width * 0.5f;
            float r = ln.x + ln.width * 0.5f;
            float t = ln.y - ln.height * 0.5f;
            float b = ln.y + ln.height * 0.5f;
            if (l < minx) minx = l;
            if (t < miny) miny = t;
            if (r > maxx) maxx = r;
            if (b > maxy) maxy = b;
            any = true;
        }
        if (!any) continue;
        LaneBox box;
        box.id = sg.id;
        box.title = sg.title;
        box.x = minx - kLanePadding;
        box.y = miny - kLanePadding - kLaneTitleBand;
        box.width = (maxx - minx) + 2.0f * kLanePadding;
        box.height = (maxy - miny) + 2.0f * kLanePadding + kLaneTitleBand;
        out.push_back(box);
    }
    return out;
}

}  // namespace mermaid
