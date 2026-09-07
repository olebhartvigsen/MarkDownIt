// Pie layout: exact geometry parity with mermaid.js (verified against the
// live oracle: tools/mermaid-oracle/pie_dump.mjs + pie_math.mjs).
//
// Constants recovered from mermaid pieRenderer.ts + rendering probes:
//   SCREEN_H = 450            fixed svg height
//   MARGIN   = 40             radius margin
//   LEGEND_RECT_SIZE = 18     legend swatch box
//   LEGEND_SPACING   = 4      legend spacing
//   RING_R = radius + outerStrokeWidth/2 = 186 (stroke-only outer ring)
//   RADIUS = SCREEN_H/2 - MARGIN = 185
//   CX = CY = pieWidth/2 = 225 (pieWidth == height == 450)
//   textPosition = 0.75       label radius factor
#ifndef MERMAID_PIE_LAYOUT_H
#define MERMAID_PIE_LAYOUT_H

#include "pie_parse.h"

#include <string>
#include <vector>

namespace mermaid {

struct PieArc {
    double start_angle = 0;  // radians, measured from 12 o'clock, clockwise
    double end_angle = 0;
    int slice_index = -1;    // index into PieDiagram::slices (unsorted)
    std::string label;
    std::string pct;         // display percent, e.g. "30%"
    // Label center in screen coords (pie center translated by caller).
    double label_x = 0;
    double label_y = 0;
    int color_index = -1;    // 0..11 palette index (arc order)
};

struct PieLegendRow {
    std::string label;
    double x = 0, y = 0;     // row top-left, in full-canvas coordinates
    int color_index = -1;
    std::string value_text;  // shown with showData: "label [value]"
};

struct LaidOutPie {
    double width = 528;
    double height = 450;
    double cx = 225, cy = 225;
    double radius = 185, ring_r = 186;
    std::string title;
    std::vector<PieArc> arcs;         // arc order (desc by value, stable)
    std::vector<PieLegendRow> legend; // arc order
    std::string error;
};

LaidOutPie LayoutPie(const PieDiagram& pie);

}  // namespace mermaid

#endif  // MERMAID_PIE_LAYOUT_H
