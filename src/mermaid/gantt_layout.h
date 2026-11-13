// Gantt layout: resolves task dates and lays out the chart geometry.
//
// Not a port of mermaid's ganttRenderer.ts (no public oracle was reused for
// this diagram kind); the geometry below is this project's own approximation,
// built to the same seam primitives the pie and sequence painters use
// (FillRect, FillPolygon, DrawText). Rows stack top to bottom: one header
// row per section, followed by that section's task rows in source order.
// A task's start is either an explicit date or "after <id>", resolved
// against the other tasks' computed end days.
#ifndef MERMAID_GANTT_LAYOUT_H
#define MERMAID_GANTT_LAYOUT_H

#include "gantt_parse.h"

#include <string>
#include <vector>

namespace mermaid {

struct GanttBar {
    int task_index = -1;     // index into GanttDiagram::tasks
    double x = 0, y = 0;
    double width = 0, height = 0;
    bool milestone = false;
    bool crit = false;
    bool active = false;
    bool done = false;
    std::string label;
    double label_x = 0, label_y = 0;  // vertical-centered text anchor
};

struct GanttSectionBand {
    std::string title;
    double y = 0, height = 0;
    double title_x = 0, title_y = 0;
    int color_index = 0;
};

struct GanttAxisTick {
    double x = 0;
    std::string label;
};

struct LaidOutGantt {
    double width = 0, height = 0;
    std::string title;
    double title_x = 0, title_y = 0;
    std::vector<GanttSectionBand> sections;
    std::vector<GanttBar> bars;
    std::vector<GanttAxisTick> ticks;
    double grid_top = 0, grid_bottom = 0;
    std::string error;
};

LaidOutGantt LayoutGantt(const GanttDiagram& g);

}  // namespace mermaid

#endif  // MERMAID_GANTT_LAYOUT_H
