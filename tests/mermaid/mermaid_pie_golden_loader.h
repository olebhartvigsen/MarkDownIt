// Pie golden loader: parses the pie oracle's JSON (pie1..3.json).
// Separate from golden_loader.h; different schema (arcs/legend, no graph).
#pragma once

#include "../../src/mermaid/pie_layout.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mermaid {

struct PieGoldenArc {
    int index = -1;
    double start = 0, end = 0;
    std::string pct;
    double label_x = 0, label_y = 0;
    std::string fill;
};

struct PieGoldenLegend {
    std::string label;
    double x = 0, y = 0;
};

struct PieGolden {
    std::string title;
    double width = 0, height = 0, cx = 0, cy = 0, radius = 0;
    std::vector<PieGoldenArc> arcs;
    std::vector<PieGoldenLegend> legend;
};

namespace piegolddetail {

struct Cursor {
    const std::string& s;
    size_t i = 0;
    explicit Cursor(const std::string& src) : s(src) {}
    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    char peek() { skip_ws(); return i < s.size() ? s[i] : '\0'; }
    char get() { skip_ws(); return i < s.size() ? s[i++] : '\0'; }
    void expect(char c) {
        skip_ws();
        if (i >= s.size() || s[i] != c)
            throw std::runtime_error(std::string("pie golden: expected '") + c + "'");
        ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"')
            throw std::runtime_error("pie golden: expected string");
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[i + 1];
                if (n == '"') out.push_back('"');
                else if (n == '\\') out.push_back('\\');
                else if (n == 'n') out.push_back('\n');
                else if (n == 't') out.push_back('\t');
                else if (n == '/') out.push_back('/');
                else out.push_back(n);
                i += 2;
            } else {
                out.push_back(s[i++]);
            }
        }
        if (i >= s.size()) throw std::runtime_error("pie golden: unterminated string");
        ++i;
        return out;
    }
    double parse_number_or_null() {
        skip_ws();
        if (i + 3 < s.size() && s.compare(i, 4, "null") == 0) {
            i += 4;
            return std::nan("");
        }
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+' || (s[i] >= '0' && s[i] <= '9')))
            ++i;
        if (start == i) throw std::runtime_error("pie golden: expected number");
        return std::strtod(s.c_str() + start, nullptr);
    }
    void skip_value() {
        skip_ws();
        if (i >= s.size()) return;
        char c = s[i];
        if (c == '"') { parse_string(); return; }
        if (c == '{' || c == '[') {
            char close = (c == '{') ? '}' : ']';
            int depth = 1; ++i;
            while (i < s.size() && depth > 0) {
                if (s[i] == '"') { parse_string(); continue; }
                if (s[i] == '{' || s[i] == '[') { ++depth; ++i; continue; }
                if (s[i] == '}' || s[i] == ']') { --depth; ++i; continue; }
                ++i;
            }
            (void)close;
            return;
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']')
            ++i;
    }
};

inline double num_or_zero(double d) { return std::isnan(d) ? 0.0 : d; }

inline PieGoldenArc parse_arc(Cursor& c) {
    PieGoldenArc a;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "index") a.index = static_cast<int>(c.parse_number_or_null());
        else if (k == "start") a.start = num_or_zero(c.parse_number_or_null());
        else if (k == "end") a.end = num_or_zero(c.parse_number_or_null());
        else if (k == "pct") a.pct = c.parse_string();
        else if (k == "label_x") a.label_x = num_or_zero(c.parse_number_or_null());
        else if (k == "label_y") a.label_y = num_or_zero(c.parse_number_or_null());
        else if (k == "fill") a.fill = c.parse_string();
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return a;
}

inline PieGoldenLegend parse_legend(Cursor& c) {
    PieGoldenLegend l;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "label") l.label = c.parse_string();
        else if (k == "x") l.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") l.y = num_or_zero(c.parse_number_or_null());
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return l;
}

}  // namespace piegolddetail

inline PieGolden LoadPieGolden(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("pie golden: cannot open " + path);
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    piegolddetail::Cursor c(src);
    PieGolden g;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "title") {
            c.skip_ws();
            if (c.peek() == '"') g.title = c.parse_string();
            else { c.skip_value(); }   // null title
        } else if (k == "width") g.width = piegolddetail::num_or_zero(c.parse_number_or_null());
        else if (k == "height") g.height = piegolddetail::num_or_zero(c.parse_number_or_null());
        else if (k == "cx") g.cx = piegolddetail::num_or_zero(c.parse_number_or_null());
        else if (k == "cy") g.cy = piegolddetail::num_or_zero(c.parse_number_or_null());
        else if (k == "radius") g.radius = piegolddetail::num_or_zero(c.parse_number_or_null());
        else if (k == "arcs") {
            c.expect('[');
            c.skip_ws();
            if (c.peek() != ']') {
                while (true) {
                    g.arcs.push_back(piegolddetail::parse_arc(c));
                    c.skip_ws();
                    if (c.peek() == ',') { c.get(); continue; }
                    break;
                }
            }
            c.expect(']');
        } else if (k == "legend") {
            c.expect('[');
            c.skip_ws();
            if (c.peek() != ']') {
                while (true) {
                    g.legend.push_back(piegolddetail::parse_legend(c));
                    c.skip_ws();
                    if (c.peek() == ',') { c.get(); continue; }
                    break;
                }
            }
            c.expect(']');
        } else {
            c.skip_value();
        }
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return g;
}

}  // namespace mermaid
