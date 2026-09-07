#include "pie_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mermaid {

namespace {

// All constants mirror mermaid pieRenderer.ts (see pie_layout.h).
constexpr double SCREEN_H = 450.0;
constexpr double MARGIN = 40.0;
constexpr double LEGEND_RECT_SIZE = 18.0;
constexpr double LEGEND_SPACING = 4.0;
constexpr double STROKE_W = 2.0;

struct ArcData {
    std::string label;
    double value = 0;
    int source_index = -1;
};

// Value text formatting for showData (`label [value]`). JS `${label}
// [${value}]` prints the raw number: 100 -> "100", 2.5 -> "2.5".
std::string FormatNumber(double v) {
    char buf[48];
    if (v == std::floor(v) && std::abs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%g", v);
    }
    return std::string(buf);
}

// Percent text exactly like mermaid: (value/sum*100).toFixed(0) + "%".
// ES toFixed(0): pick the integer minimizing |n - x|; on a tie pick the
// LARGER n (spec: "the number n ... is the largest"). For exact .5 values
// this is floor(x + 0.5). Note printf %.0f uses round-half-even, which
// differs from JS at 61.5 (62 vs 61.5→61) — so we do NOT use printf.
std::string PercentText(double value, double sum) {
    double pct = sum > 0 ? std::abs(value) / sum * 100.0 : 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::floor(pct + 0.5)));
    return std::string(buf) + "%";
}

}  // namespace

LaidOutPie LayoutPie(const PieDiagram& pie) {
    LaidOutPie out;
    if (!pie.error.empty()) {
        out.error = pie.error;
        return out;
    }

    // Build arcs in mermaid order: sort desc by value, stable.
    std::vector<ArcData> sorted;
    sorted.reserve(pie.slices.size());
    for (size_t i = 0; i < pie.slices.size(); ++i) {
        sorted.push_back({pie.slices[i].label, pie.slices[i].value,
                          static_cast<int>(i)});
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const ArcData& a, const ArcData& b) {
                         return b.value < a.value;
                     });

    // d3.pie default: startAngle = 0 (12 o'clock), endAngle = 2π, clockwise.
    double sum = 0;
    for (const auto& s : sorted) sum += s.value;

    const double radius = (SCREEN_H / 2.0) - MARGIN;  // 185
    const double text_pos = 0.75;
    double a0 = 0.0;
    const double full = 2.0 * 3.14159265358979323846;
    double cx = SCREEN_H / 2.0, cy = SCREEN_H / 2.0;

    int color_idx = 0;
    for (const auto& s : sorted) {
        double a1 = (sum > 0) ? a0 + (s.value / sum) * full : a0;
        int idx = out.arcs.size();
        out.arcs.push_back(PieArc{});
        PieArc& arc = out.arcs.back();
        arc.start_angle = a0;
        arc.end_angle = a1;
        arc.slice_index = s.source_index;
        arc.label = s.label;
        arc.pct = PercentText(s.value, sum);
        double mid = (a0 + a1) / 2.0;
        // Screen coords: x = cx + r*sin(mid), y = cy - r*cos(mid)
        // (12 o'clock = -y direction, clockwise sweep).
        arc.label_x = cx + radius * text_pos * std::sin(mid);
        arc.label_y = cy - radius * text_pos * std::cos(mid);
        arc.color_index = color_idx++;
        a0 = a1;
    }

    // Legend rows, in arc order.
    const double legend_h = LEGEND_RECT_SIZE + LEGEND_SPACING;  // 22
    const double offset = legend_h * static_cast<double>(out.arcs.size()) / 2.0;
    for (size_t k = 0; k < out.arcs.size(); ++k) {
        PieLegendRow row;
        // With showData the legend TEXT is \"label [value]\" (the golden
        // oracle dumps exactly that string as label).
        std::string label = sorted[k].label;
        if (pie.show_data) {
            label = label + " [" + FormatNumber(sorted[k].value) + "]";
        }
        row.label = label;
        row.color_index = out.arcs[k].color_index;
        row.x = 12.0 * LEGEND_RECT_SIZE;  // 216
        row.y = k * legend_h - offset;
        out.legend.push_back(row);
    }

    // Canvas size: width = pieWidth + MARGIN + LEGEND_RECT_SIZE +
    // LEGEND_SPACING + longest legend text; height fixed 450. Longest text
    // width uses the oracle's measurement model (8 px per UTF-16 unit —
    // jsdom getBoundingClientRect shim: width = n * 8).
    double longest_text = 0.0;
    for (const auto& row : out.legend) {
        double units = 0.0;
        for (unsigned char c : row.label) {
            if ((c & 0xC0) != 0x80) ++units;  // UTF-16 units for BMP chars
        }
        longest_text = std::max(longest_text, units * 8.0);
    }
    (void)STROKE_W;
    out.width = SCREEN_H + MARGIN + LEGEND_RECT_SIZE + LEGEND_SPACING +
                longest_text;
    out.height = SCREEN_H;
    out.cx = cx;
    out.cy = cy;
    out.radius = radius;
    out.ring_r = radius + 1.0;  // outerStrokeWidth/2 = 1
    out.title = pie.title;
    return out;
}

}  // namespace mermaid
