// Public mermaid layout wrapper. Chains the six phases and reports bbox.
#include "layout.h"
#include "layout_internal.h"
#include "parse.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace mermaid {

// `<br/>`-aware label sizing, the exact mirror of dump.mjs sizeFor with
// CHAR_W=8.4, LINE_H=19, PADDING=15: a label with line breaks wraps into N
// visual lines; width counts the longest line.
static void StubSizeForLabel(const std::string& label, float& width,
                             float& height) {
    const double CHAR_W = 8.4, LINE_H = 19.0, PADDING = 15.0;
    std::vector<std::string> lines = SplitLabelLines(label);
    double longest = 0.0;
    for (const auto& l : lines) {
        // JS String.length counts UTF-16 code units; our std::string is
        // UTF-8. For characters in the BMP (Latin-1 supplement, Greek,
        // most CJK) one UTF-8 char maps to one UTF-16 unit for 1/2-byte
        // sequences; a 3-byte UTF-8 char is one UTF-16 unit as well. Only
        // astral-plane chars (4-byte UTF-8) take two UTF-16 units. Node
        // labels in Mermaid sources are BMP-only in practice, so count
        // non-continuation bytes.
        double units = 0.0;
        for (unsigned char c : l) {
            if ((c & 0xC0) != 0x80) ++units;  // skip continuation bytes
        }
        longest = std::max(longest, units);
    }
    double raw_w = longest * CHAR_W;
    if (raw_w < 14.0) raw_w = 14.0;
    width = static_cast<float>(raw_w + PADDING * 2.0);
    height = static_cast<float>(
        (lines.empty() ? 1.0 : static_cast<double>(lines.size())) * LINE_H +
        PADDING * 2.0);
}

// Mermaid line-break separators for node labels: <br/>, <br>, <br /> and the
// literal two-character sequence backslash-n. Trailing empty segments are
// dropped (mermaid skips empty lines).
std::vector<std::string> SplitLabelLines(const std::string& label) {
    static const char* const seps[] = {"<br/>", "<br>", "<br />", "\\n"};
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < label.size()) {
        bool matched = false;
        for (const char* sep : seps) {
            size_t slen = std::strlen(sep);
            if (label.compare(i, slen, sep) == 0) {
                lines.push_back(cur);
                cur.clear();
                i += slen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            cur.push_back(label[i]);
            ++i;
        }
    }
    lines.push_back(cur);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

LayoutGraph FlowchartToLayoutGraph(const Flowchart& flow) {
    LayoutGraph g;
    g.nodes.reserve(flow.nodes.size());
    for (const auto& n : flow.nodes) {
        LayoutNode ln;
        ln.id = static_cast<int>(g.nodes.size());
        ln.label = n.label.empty() ? n.id : n.label;
        StubSizeForLabel(ln.label, ln.width, ln.height);
        switch (n.shape) {
            case Shape::Rect:    ln.shape = NodeShape::Rect; break;
            case Shape::Round:   ln.shape = NodeShape::Round; break;
            case Shape::Stadium: ln.shape = NodeShape::Stadium; break;
            case Shape::Diamond: ln.shape = NodeShape::Diamond; break;
            case Shape::Circle:  ln.shape = NodeShape::Circle; break;
        }
        g.nodes.push_back(ln);
    }
    g.edges.reserve(flow.edges.size());
    for (const auto& e : flow.edges) {
        LayoutEdge le;
        le.from = e.from;
        le.to = e.to;
        le.minlen = e.minlen > 0 ? e.minlen : 1;
        le.weight = e.weight > 0 ? e.weight : 1;
        le.label = e.label;
        le.style = e.style;
        le.head = e.head;
        g.edges.push_back(le);
    }
    return g;
}

// Apply rankdir post-layout transform. Layout phases produce TB-relative
// coordinates (x grows right, y grows down). Swap into the requested
// direction before the final margin/bbox pass.
static void ApplyRankdir(LayoutGraph& g, Dir dir) {
    if (dir == Dir::TB) return;

    // Compute pre-transform bbox around node halves and route points.
    double max_x = 0.0, max_y = 0.0;
    for (const auto& n : g.nodes) {
        double r = static_cast<double>(n.x) + static_cast<double>(n.width)  * 0.5;
        double b = static_cast<double>(n.y) + static_cast<double>(n.height) * 0.5;
        if (r > max_x) max_x = r;
        if (b > max_y) max_y = b;
    }
    for (const auto& e : g.edges) {
        for (const auto& p : e.route) {
            if (p.x > max_x) max_x = p.x;
            if (p.y > max_y) max_y = p.y;
        }
    }

    if (dir == Dir::BT) {
        for (auto& n : g.nodes) n.y = static_cast<float>(max_y - n.y);
        for (auto& e : g.edges)
            for (auto& p : e.route) p.y = max_y - p.y;
        return;
    }

    // LR / RL: swap axes. Node widths/heights were already pre-swapped
    // before layout so the coordinates transpose cleanly here.
    for (auto& n : g.nodes) {
        std::swap(n.x, n.y);
        std::swap(n.width, n.height);
    }
    for (auto& e : g.edges) {
        for (auto& p : e.route) std::swap(p.x, p.y);
    }

    if (dir == Dir::RL) {
        // Mirror x across new width (which was the old max_y).
        double new_w = max_y;
        for (auto& n : g.nodes) n.x = static_cast<float>(new_w - n.x);
        for (auto& e : g.edges)
            for (auto& p : e.route) p.x = new_w - p.x;
    }
}

LaidOutFlowchart LayoutFlowchart(const Flowchart& flow, const LayoutParams& p) {
    LaidOutFlowchart out;
    LayoutGraph g = FlowchartToLayoutGraph(flow);
    if (g.nodes.empty()) return out;

    // For LR/RL, pre-swap node dimensions so the TB pipeline lays them out
    // along what will become the horizontal rank axis after the post-layout
    // axis swap. Without this, TB row heights (49) leak into LR column
    // widths and the geometry does not match dagre.
    const bool axis_swap = (flow.dir == Dir::LR || flow.dir == Dir::RL);
    if (axis_swap) {
        for (auto& n : g.nodes) std::swap(n.width, n.height);
    }

    MakeAcyclic(g);
    AssignRanks(g);
    Normalize(g);
    Order(g);
    AssignCoordinates(g, p);
    RouteEdges(g, p);
    Denormalize(g);

    ApplyRankdir(g, flow.dir);

    // Shift by margin so nothing sits at (0,0) exactly and bbox includes it.
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

    // Bounding box: max of node right/bottom edges and edge route points.
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

    // Swimlane bounding boxes, in the same coordinate system as nodes/edges.
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
