#include "pie_parse.h"

#include <cmath>
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

// Strips one outer pair of double quotes if present.
std::string StripQuotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool ParseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') {
        neg = s[i] == '-';
        ++i;
    }
    size_t digits = 0, dots = 0;
    double v = 0;
    while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])) ||
                            s[i] == '.')) {
        if (s[i] == '.') {
            if (++dots > 1) return false;
        } else {
            v = v * 10 + (s[i] - '0');
            ++digits;
        }
        ++i;
    }
    if (i < s.size()) return false;   // trailing junk
    if (dots == 1) {
        // Re-parse through mantissa form for fractional part.
        double frac = 0, scale = 0.1;
        bool in_frac = false;
        double whole = 0;
        for (char c : s) {
            if (c == '.') { in_frac = true; continue; }
            if (c == '-' || c == '+') continue;
            if (in_frac) {
                frac += (c - '0') * scale;
                scale *= 0.1;
            } else {
                whole = whole * 10 + (c - '0');
            }
        }
        v = whole + frac;
    }
    out = neg ? -v : v;
    return digits > 0;
}

}  // namespace

bool PieParseHeaderLine(const std::string& line, PieDiagram& pie) {
    // pie [showData] [title <text>]
    if (line.compare(0, 3, "pie") != 0) return false;
    if (line.size() > 3 && !IsSpace(line[3])) return false;
    size_t pos = 3;
    while (pos < line.size() && IsSpace(line[pos])) ++pos;
    std::string rest = line.substr(pos);

    if (rest.compare(0, 8, "showData") == 0 &&
        (rest.size() == 8 || IsSpace(rest[8]))) {
        pie.show_data = true;
        size_t skip = 8;
        while (skip < rest.size() && IsSpace(rest[skip])) ++skip;
        rest = rest.substr(skip);
    }

    if (rest.compare(0, 5, "title") == 0 &&
        (rest.size() == 5 || IsSpace(rest[5]))) {
        pie.title = Trim(rest.substr(5));
    } else if (!rest.empty()) {
        // Unknown tokens after pie: tolerate (mermaid ignores unknown flags).
    }
    return true;
}

PieDiagram ParsePie(std::string_view src) {
    PieDiagram pie;
    bool header_seen = false;

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
        if (line[0] == '"' || line[0] == '\'' || isdigit(static_cast<unsigned char>(line[0]))) {
            // Data line: "label" : value  or  label : value.
            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                pie.error = "malformed pie data line (missing ':')";
                return pie;
            }
            std::string label = StripQuotes(Trim(line.substr(0, colon)));
            double value = 0;
            if (!ParseDouble(Trim(line.substr(colon + 1)), value)) {
                pie.error = "malformed pie value";
                return pie;
            }
            pie.slices.push_back({std::move(label), value});
            continue;
        }
        // Header / directives before any data lines.
        if (!header_seen) {
            if (PieParseHeaderLine(line, pie)) {
                header_seen = true;
                continue;
            }
            pie.error = "not a pie diagram";
            return pie;
        }
        // Unknown line after header: ignore silently (mermaid tolerates).
    }

    if (!header_seen && pie.slices.empty()) {
        pie.error = "not a pie diagram";
        return pie;
    }
    return pie;
}

}  // namespace mermaid
