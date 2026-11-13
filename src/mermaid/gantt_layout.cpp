#include "gantt_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace mermaid {

namespace {

// Days since the civil epoch (1970-01-01), proleptic Gregorian. The
// well-known constant-time algorithm (Howard Hinnant, public domain),
// also used by std::chrono::year_month_day in recent standard libraries.
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy =
        (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void CivilFromDays(int64_t z, int* y, int* m, int* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yr = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    *m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    *y = static_cast<int>(yr + (*m <= 2 ? 1 : 0));
}

// Substitutes %d, %m, %Y, %y in an axisFormat-style string. Falls back
// to "dd/mm" when the format has no recognized tokens, so the axis
// still reads even for an mermaid axisFormat this parser does not know.
std::string FormatAxisDate(const std::string& fmt, int y, int m, int d) {
    char buf[8];
    std::string out;
    bool matched = false;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            char c = fmt[i + 1];
            if (c == 'd') { std::snprintf(buf, sizeof(buf), "%02d", d); out += buf; matched = true; ++i; continue; }
            if (c == 'm') { std::snprintf(buf, sizeof(buf), "%02d", m); out += buf; matched = true; ++i; continue; }
            if (c == 'Y') { std::snprintf(buf, sizeof(buf), "%04d", y); out += buf; matched = true; ++i; continue; }
            if (c == 'y') { std::snprintf(buf, sizeof(buf), "%02d", y % 100); out += buf; matched = true; ++i; continue; }
        }
        out += fmt[i];
    }
    if (!matched) {
        std::snprintf(buf, sizeof(buf), "%02d/%02d", d, m);
        return buf;
    }
    return out;
}

constexpr double kBarHeight = 20.0;
constexpr double kRowGap = 8.0;
constexpr double kRowHeight = kBarHeight + kRowGap;
constexpr double kTopPadding = 50.0;
constexpr double kLeftPadding = 12.0;
constexpr double kRightPadding = 20.0;
constexpr double kMinDayWidth = 6.0;
constexpr double kMaxDayWidth = 34.0;
constexpr double kTargetChartWidth = 900.0;

const uint32_t kSectionBandColors[] = {0, 1};  // alternates plain/banded

}  // namespace

LaidOutGantt LayoutGantt(const GanttDiagram& g) {
    LaidOutGantt lo;
    if (!g.error.empty()) {
        lo.error = g.error;
        return lo;
    }
    if (g.tasks.empty()) {
        lo.error = "no tasks";
        return lo;
    }

    // Resolve every task's absolute start day. Date tasks resolve in one
    // step; "after <id>" tasks resolve once their reference's end day is
    // known, so this runs as a bounded number of passes (one per task is
    // always enough: at most one unresolved dependency chain per pass).
    std::vector<int64_t> start_day(g.tasks.size(), 0);
    std::vector<int64_t> end_day(g.tasks.size(), 0);
    std::vector<bool> resolved(g.tasks.size(), false);
    std::map<std::string, size_t> id_to_index;
    for (size_t t = 0; t < g.tasks.size(); ++t) {
        if (!g.tasks[t].id.empty()) id_to_index[g.tasks[t].id] = t;
    }

    for (size_t pass = 0; pass < g.tasks.size() + 1; ++pass) {
        bool progressed = false;
        for (size_t t = 0; t < g.tasks.size(); ++t) {
            if (resolved[t]) continue;
            const GanttTask& task = g.tasks[t];
            if (task.start_kind == GanttStartKind::Date) {
                start_day[t] = DaysFromCivil(task.start_year, task.start_month,
                                             task.start_day_of_month);
                end_day[t] = start_day[t] +
                             static_cast<int64_t>(std::llround(task.duration_days));
                resolved[t] = true;
                progressed = true;
            } else {
                auto it = id_to_index.find(task.after_id);
                if (it != id_to_index.end() && resolved[it->second]) {
                    start_day[t] = end_day[it->second];
                    end_day[t] = start_day[t] +
                                 static_cast<int64_t>(std::llround(task.duration_days));
                    resolved[t] = true;
                    progressed = true;
                }
            }
        }
        if (!progressed) break;
    }
    // Any task that never resolved (a dangling "after" reference) keeps
    // day 0, so it still renders rather than disappearing.
    for (size_t t = 0; t < g.tasks.size(); ++t) {
        if (!resolved[t]) end_day[t] = start_day[t];
    }

    int64_t min_day = start_day[0], max_day = end_day[0];
    for (size_t t = 1; t < g.tasks.size(); ++t) {
        min_day = std::min(min_day, start_day[t]);
        max_day = std::max(max_day, end_day[t]);
    }
    const int64_t total_days = std::max<int64_t>(1, max_day - min_day);
    double day_width = kTargetChartWidth / static_cast<double>(total_days);
    day_width = std::clamp(day_width, kMinDayWidth, kMaxDayWidth);
    const double chart_left = kLeftPadding;

    // Build rows: one header row per section (even an implicit section
    // when no `section` line appeared, so ungrouped tasks still render),
    // then that section's tasks in source order.
    const bool has_sections = !g.sections.empty();
    double y = kTopPadding;
    lo.grid_top = y;

    auto emit_section = [&](int section_index, const std::string& title) {
        GanttSectionBand band;
        band.title = title;
        band.y = y;
        band.title_x = chart_left;
        band.title_y = y + kBarHeight * 0.5;
        band.color_index = static_cast<int>(lo.sections.size()) %
                           (sizeof(kSectionBandColors) / sizeof(kSectionBandColors[0]));
        y += kRowHeight;
        size_t rows_in_section = 0;
        for (size_t t = 0; t < g.tasks.size(); ++t) {
            if (g.tasks[t].section_index != section_index) continue;
            GanttBar bar;
            bar.task_index = static_cast<int>(t);
            bar.crit = g.tasks[t].crit;
            bar.active = g.tasks[t].active;
            bar.done = g.tasks[t].done;
            bar.milestone = g.tasks[t].milestone;
            bar.label = g.tasks[t].label;
            bar.x = chart_left + static_cast<double>(start_day[t] - min_day) * day_width;
            const double span_days =
                static_cast<double>(end_day[t] - start_day[t]);
            bar.width = bar.milestone ? kBarHeight
                                      : std::max(2.0, span_days * day_width);
            bar.y = y + (kRowHeight - kBarHeight) * 0.5;
            bar.height = kBarHeight;
            bar.label_x = bar.x + bar.width + 6.0;
            bar.label_y = bar.y + kBarHeight * 0.5;
            lo.bars.push_back(bar);
            y += kRowHeight;
            ++rows_in_section;
        }
        band.height = kRowHeight + static_cast<double>(rows_in_section) * kRowHeight;
        lo.sections.push_back(band);
    };

    if (has_sections) {
        for (size_t s = 0; s < g.sections.size(); ++s) {
            emit_section(static_cast<int>(s), g.sections[s].title);
        }
    } else {
        emit_section(-1, "");
    }

    lo.grid_bottom = y;

    // Axis ticks: one every 7 days across the resolved span, formatted
    // with axisFormat when the diagram declared one.
    const std::string axis_fmt = g.axis_format.empty() ? "%d/%m" : g.axis_format;
    for (int64_t day = min_day; day <= max_day; day += 7) {
        int ty = 0, tm = 0, td = 0;
        CivilFromDays(day, &ty, &tm, &td);
        GanttAxisTick tick;
        tick.x = chart_left + static_cast<double>(day - min_day) * day_width;
        tick.label = FormatAxisDate(axis_fmt, ty, tm, td);
        lo.ticks.push_back(tick);
    }

    lo.title = g.title;
    lo.title_x = chart_left;
    lo.title_y = 20.0;
    lo.width = chart_left + static_cast<double>(total_days) * day_width + kRightPadding;
    lo.height = lo.grid_bottom + 10.0;
    return lo;
}

}  // namespace mermaid
