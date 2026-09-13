// Class golden loader: parses the class oracle's JSON (cls1..cls5.json).
// Reuses the permissive cursor pattern from golden_loader.h.
#pragma once

#include "../../src/mermaid/class_layout.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mermaid {

struct ClassGoldenCanvas {
    double width = 0, height = 0;
    double startx = 0, starty = 0;
    double vbwidth = 0, vbheight = 0;
};

struct ClassGoldenNode {
    std::string id;
    double x = 0, y = 0;  // cx / cy
    double w = 0, h = 0;
    std::string title;
};

struct ClassGoldenEdge {
    std::string from, to;
    std::string start_marker, end_marker, pattern;
    std::vector<std::pair<double, double>> points;
};

struct ClassGoldenLabel {
    std::string kind;   // "label" | "terminal"
    std::string text;
    double x = 0, y = 0;
    double w = 0, h = 0;
    double ix = 0, iy = 0;
};

struct ClassGolden {
    ClassGoldenCanvas canvas;
    std::vector<ClassGoldenNode> nodes;
    std::vector<ClassGoldenEdge> edges;
    std::vector<ClassGoldenLabel> labels;
};

class ClassJsonCursor {
public:
    const std::string& s;
    size_t i = 0;
    explicit ClassJsonCursor(const std::string& src) : s(src) {}
    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    char peek() { skip_ws(); return i < s.size() ? s[i] : '\0'; }
    char get() { skip_ws(); return i < s.size() ? s[i++] : '\0'; }
    void expect(char c) {
        skip_ws();
        if (i >= s.size() || s[i] != c) throw std::runtime_error("class golden: expected char");
        ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"') throw std::runtime_error("class golden: expected string");
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
        if (i >= s.size()) throw std::runtime_error("class golden: unterminated string");
        ++i;
        return out;
    }
    double parse_number_or_null() {
        skip_ws();
        if (i + 3 < s.size() && s.compare(i, 4, "null") == 0) { i += 4; return std::nan(""); }
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+' || (s[i] >= '0' && s[i] <= '9')))
            ++i;
        if (start == i) throw std::runtime_error("class golden: expected number");
        return std::strtod(s.c_str() + start, nullptr);
    }
    bool try_key(const std::string& key) {
        size_t save = i;
        skip_ws();
        if (i + key.size() + 1 <= s.size() && s.compare(i, key.size(), key) == 0) {
            i += key.size();
            skip_ws();
            if (i < s.size() && s[i] == ':') { expect(':'); return true; }
        }
        i = save;
        return false;
    }
    void skip_member() {
        parse_string();  // key
        expect(':');
        skip_value();
    }
    void skip_value() {
        skip_ws();
        if (i >= s.size()) return;
        char c = s[i];
        if (c == '"') { parse_string(); return; }
        if (c == '{' || c == '[') {
            char open = c, close = (c == '{') ? '}' : ']';
            get();
            while (peek() != close) {
                if (peek() == '\0') throw std::runtime_error("class golden: unterminated");
                if (open == '{') skip_member(); else skip_value();
                if (peek() == ',') get();
            }
            get();
            return;
        }
        parse_number_or_null();
    }
};

inline ClassGolden LoadClassGolden(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "class golden: cannot open: %s", path.c_str());
        std::fputc(0x0A, stderr);
        throw std::runtime_error("class golden: open failed");
    }
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();  // cursor holds a reference; keep it alive
    ClassJsonCursor c(src);
    ClassGolden g;
    if (c.peek() != '{') {
        std::fprintf(stderr, "class golden: no object at offset %zu (first: %c)\n",
                     c.i, c.peek());
        throw std::runtime_error("class golden: no object");
    }
    const std::size_t kAt = c.i;

    auto read_pair = [&]() -> std::pair<double, double> {
        if (c.peek() == '[') {
            c.expect('[');
            double a = c.parse_number_or_null();
            c.expect(',');
            double b = c.parse_number_or_null();
            c.expect(']');
            return {a, b};
        }
        // Object form: {"x": a, "y": b}
        double a = 0, b = 0;
        c.expect('{');
        while (c.peek() != '}') {
            std::string k = c.parse_string();
            c.expect(':');
            double v = c.parse_number_or_null();
            if (k == "x") a = v;
            else if (k == "y") b = v;
            if (c.peek() == ',') c.get();
        }
        c.expect('}');
        return {a, b};
    };

    try {
    c.expect('{');
    while (c.peek() != '}') {
        std::string key = c.parse_string();
        c.expect(':');
        if (key == "canvas") {
            c.expect('{');
            while (c.peek() != '}') {
                std::string k2 = c.parse_string();
                c.expect(':');
                double v = c.parse_number_or_null();
                if (k2 == "width") g.canvas.width = v;
                else if (k2 == "height") g.canvas.height = v;
                else if (k2 == "startx") g.canvas.startx = v;
                else if (k2 == "starty") g.canvas.starty = v;
                else if (k2 == "vbwidth") g.canvas.vbwidth = v;
                else if (k2 == "vbheight") g.canvas.vbheight = v;
                if (c.peek() == ',') c.get();
            }
            c.expect('}');
        } else if (key == "nodes") {
            c.expect('[');
            while (c.peek() != ']') {
                ClassGoldenNode n;
                c.expect('{');
                while (c.peek() != '}') {
                    std::string k2 = c.parse_string();
                    c.expect(':');
                    if (k2 == "id") n.id = c.parse_string();
                    else if (k2 == "cx") n.x = c.parse_number_or_null();
                    else if (k2 == "cy") n.y = c.parse_number_or_null();
                    else if (k2 == "w") n.w = c.parse_number_or_null();
                    else if (k2 == "h") n.h = c.parse_number_or_null();
                    else if (k2 == "title") n.title = c.parse_string();
                    else c.skip_value();
                    if (c.peek() == ',') c.get();
                }
                c.expect('}');
                g.nodes.push_back(n);
                if (c.peek() == ',') c.get();
            }
            c.expect(']');
        } else if (key == "edges") {
            c.expect('[');
            while (c.peek() != ']') {
                ClassGoldenEdge e;
                c.expect('{');
                while (c.peek() != '}') {
                    std::string k2 = c.parse_string();
                    c.expect(':');
                    if (k2 == "from") e.from = c.parse_string();
                    else if (k2 == "to") e.to = c.parse_string();
                    else if (k2 == "start_marker") e.start_marker = c.parse_string();
                    else if (k2 == "end_marker") e.end_marker = c.parse_string();
                    else if (k2 == "pattern") e.pattern = c.parse_string();
                    else if (k2 == "points") {
                        c.expect('[');
                        while (c.peek() != ']') {
                            e.points.push_back(read_pair());
                            if (c.peek() == ',') c.get();
                        }
                        c.expect(']');
                    } else c.skip_value();
                    if (c.peek() == ',') c.get();
                }
                c.expect('}');
                g.edges.push_back(e);
                if (c.peek() == ',') c.get();
            }
            c.expect(']');
        } else if (key == "edge_labels") {
            c.expect('[');
            while (c.peek() != ']') {
                ClassGoldenLabel l;
                c.expect('{');
                while (c.peek() != '}') {
                    std::string k2 = c.parse_string();
                    c.expect(':');
                    if (k2 == "kind") l.kind = c.parse_string();
                    else if (k2 == "text") l.text = c.parse_string();
                    else if (k2 == "x") l.x = c.parse_number_or_null();
                    else if (k2 == "y") l.y = c.parse_number_or_null();
                    else if (k2 == "w") l.w = c.parse_number_or_null();
                    else if (k2 == "h") l.h = c.parse_number_or_null();
                    else if (k2 == "ix") l.ix = c.parse_number_or_null();
                    else if (k2 == "iy") l.iy = c.parse_number_or_null();
                    else c.skip_value();
                    if (c.peek() == ',') c.get();
                }
                c.expect('}');
                g.labels.push_back(l);
                if (c.peek() == ',') c.get();
            }
            c.expect(']');
        } else {
            c.skip_value();
        }
        if (c.peek() == ',') c.get();
    }
    c.expect('}');
    } catch (const std::runtime_error& err) {
        std::fprintf(stderr, "class golden parse error at offset %zu: %s",
                     c.i, err.what());
        std::fputc(0x0A, stderr);
        throw;
    }
    return g;
}

}  // namespace mermaid