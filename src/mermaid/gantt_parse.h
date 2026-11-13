// Gantt diagram: parse `gantt` definitions.
//
// Grammar (subset mermaid-compatible):
//   gantt
//   title <text>
//   dateFormat <format>      e.g. DD/MM/YYYY, YYYY-MM-DD
//   axisFormat <format>      e.g. %d/%m (kept for the axis label painter)
//   section <name>
//   <label> :[tag, ...] <id>, <start>, <duration>
//   <label> :[tag, ...] <start>, <duration>        (id auto-generated)
// where <start> is either a date in dateFormat, or "after <id>" (one
// reference), and <duration> is a number followed by d/w/h (days, weeks,
// hours). Recognized tags: crit, active, done, milestone. A milestone
// task has zero duration by convention, drawn as a diamond, not a bar.
// Comments (%%...) and unknown directive lines are tolerated, the same
// way the pie and sequence parsers tolerate them.
#ifndef MERMAID_GANTT_PARSE_H
#define MERMAID_GANTT_PARSE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mermaid {

enum class GanttStartKind { Date, AfterRef };

struct GanttTask {
    std::string label;
    std::string id;
    bool milestone = false;
    bool crit = false;
    bool active = false;
    bool done = false;
    int section_index = -1;

    GanttStartKind start_kind = GanttStartKind::Date;
    // Valid when start_kind == Date (already validated against the
    // declared dateFormat, calendar-checked).
    int start_year = 0, start_month = 1, start_day_of_month = 1;
    // Valid when start_kind == AfterRef: the referenced task's id. Only
    // one reference is supported (matches every real-world fixture).
    std::string after_id;

    double duration_days = 0;  // 0 for a milestone
};

struct GanttSection {
    std::string title;
};

struct GanttDiagram {
    std::string title;
    std::string date_format = "YYYY-MM-DD";
    std::string axis_format;
    std::vector<GanttSection> sections;
    std::vector<GanttTask> tasks;
    std::string error;  // non-empty means parse failed
};

// Parses `gantt ...` source (without code fences). On error, error is set
// and the returned diagram is empty.
GanttDiagram ParseGantt(std::string_view src);

}  // namespace mermaid

#endif  // MERMAID_GANTT_PARSE_H
