#include "gantt_parse.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace mermaid {

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) ++b;
    while (e > b && IsSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

bool StartsWithWord(const std::string& s, const std::string& word) {
    if (s.compare(0, word.size(), word) != 0) return false;
    return s.size() == word.size() || IsSpace(s[word.size()]);
}

std::string After(const std::string& s, size_t word_len) {
    size_t pos = word_len;
    while (pos < s.size() && IsSpace(s[pos])) ++pos;
    return s.substr(pos);
}

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            out.push_back(Trim(std::string_view(s).substr(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The order D/M/Y components appear in a dateFormat string, e.g.
// "DD/MM/YYYY" -> {D, M, Y}, "YYYY-MM-DD" -> {Y, M, D}. Unrecognized
// formats fall back to the common DD/MM/YYYY order.
std::vector<char> DateFieldOrder(const std::string& fmt) {
    std::vector<char> order;
    for (size_t i = 0; i < fmt.size();) {
        char c = fmt[i];
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == 'D' || upper == 'M' || upper == 'Y') {
            if (order.empty() || order.back() != upper) order.push_back(upper);
            ++i;
        } else {
            ++i;
        }
    }
    if (order.size() != 3) return {'D', 'M', 'Y'};
    return order;
}

// Splits a date value by any run of non-digit characters, e.g.
// "02/04/2026" -> {"02","04","2026"}.
std::vector<std::string> SplitDigitsGroups(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool ParseDate(const std::string& value, const std::string& date_format,
               int* year, int* month, int* day) {
    const std::vector<char> order = DateFieldOrder(date_format);
    const std::vector<std::string> groups = SplitDigitsGroups(value);
    if (groups.size() != 3) return false;
    int y = 0, m = 0, d = 0;
    for (size_t i = 0; i < 3; ++i) {
        int v = 0;
        for (char c : groups[i]) v = v * 10 + (c - '0');
        if (order[i] == 'Y') {
            y = (groups[i].size() <= 2) ? 2000 + v : v;
        } else if (order[i] == 'M') {
            m = v;
        } else {
            d = v;
        }
    }
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    *year = y;
    *month = m;
    *day = d;
    return true;
}

bool ParseDuration(const std::string& value, double* days) {
    if (value.empty()) return false;
    size_t i = 0;
    while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) ++i;
    if (i == 0) return false;
    double n = 0;
    for (size_t k = 0; k < i; ++k) n = n * 10 + (value[k] - '0');
    if (i >= value.size()) return false;
    char unit = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    switch (unit) {
        case 'd': *days = n; return true;
        case 'w': *days = n * 7.0; return true;
        case 'h': *days = n / 24.0; return true;
        case 'm': *days = n * 30.0; return true;  // month, approximate
        default: return false;
    }
}

const char* kTagWords[] = {"crit", "active", "done", "milestone"};

bool IsTagWord(const std::string& token) {
    const std::string low = ToLower(token);
    for (const char* tag : kTagWords) {
        if (low == tag) return true;
    }
    return false;
}

}  // namespace

GanttDiagram ParseGantt(std::string_view src) {
    GanttDiagram g;
    bool header_seen = false;
    int current_section = -1;
    int auto_id_counter = 0;

    size_t i = 0, n = src.size();
    auto next_line = [&](std::string& out) -> bool {
        if (i >= n) return false;
        size_t j = i;
        while (j < n && src[j] != '\n') ++j;
        out = Trim(src.substr(i, j - i));
        i = (j < n) ? j + 1 : j;
        return true;
    };

    std::string line;
    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;

        if (!header_seen) {
            if (line != "gantt" && !StartsWithWord(line, "gantt")) {
                g.error = "not a gantt diagram";
                return g;
            }
            header_seen = true;
            continue;
        }

        if (StartsWithWord(line, "title")) {
            g.title = After(line, 5);
            continue;
        }
        if (StartsWithWord(line, "dateFormat")) {
            g.date_format = After(line, 10);
            continue;
        }
        if (StartsWithWord(line, "axisFormat")) {
            g.axis_format = After(line, 10);
            continue;
        }
        if (StartsWithWord(line, "excludes") ||
            StartsWithWord(line, "todayMarker") ||
            StartsWithWord(line, "tickInterval") ||
            StartsWithWord(line, "weekday")) {
            continue;  // tolerated, not modeled
        }
        if (StartsWithWord(line, "section")) {
            GanttSection s;
            s.title = After(line, 7);
            g.sections.push_back(std::move(s));
            current_section = static_cast<int>(g.sections.size()) - 1;
            continue;
        }

        // A task line: "<label> :<meta>".
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            // A bare directive this parser does not know: tolerate, the
            // way the pie parser tolerates unknown lines after its header.
            continue;
        }
        GanttTask task;
        task.label = Trim(std::string_view(line).substr(0, colon));
        task.section_index = current_section;
        const std::string meta = Trim(std::string_view(line).substr(colon + 1));
        std::vector<std::string> tokens = SplitComma(meta);

        std::vector<std::string> rest;
        for (const std::string& tok : tokens) {
            if (tok.empty()) continue;
            const std::string low = ToLower(tok);
            if (IsTagWord(tok)) {
                if (low == "crit") task.crit = true;
                else if (low == "active") task.active = true;
                else if (low == "done") task.done = true;
                else if (low == "milestone") task.milestone = true;
                continue;
            }
            rest.push_back(tok);
        }

        std::string id_token, start_token, duration_token;
        if (rest.size() >= 3) {
            id_token = rest[rest.size() - 3];
            start_token = rest[rest.size() - 2];
            duration_token = rest[rest.size() - 1];
        } else if (rest.size() == 2) {
            id_token = "task" + std::to_string(++auto_id_counter);
            start_token = rest[0];
            duration_token = rest[1];
        } else {
            g.error = "malformed gantt task line (missing id/start/duration)";
            return g;
        }
        task.id = id_token;

        if (StartsWithWord(start_token, "after")) {
            task.start_kind = GanttStartKind::AfterRef;
            task.after_id = Trim(After(start_token, 5));
            if (task.after_id.empty()) {
                g.error = "malformed gantt 'after' reference";
                return g;
            }
        } else {
            task.start_kind = GanttStartKind::Date;
            if (!ParseDate(start_token, g.date_format, &task.start_year,
                           &task.start_month, &task.start_day_of_month)) {
                g.error = "malformed gantt start date: " + start_token;
                return g;
            }
        }

        if (task.milestone) {
            task.duration_days = 0;
        } else if (!ParseDuration(duration_token, &task.duration_days)) {
            g.error = "malformed gantt duration: " + duration_token;
            return g;
        }

        g.tasks.push_back(std::move(task));
    }

    if (!header_seen) {
        g.error = "not a gantt diagram";
        return g;
    }
    return g;
}

}  // namespace mermaid
