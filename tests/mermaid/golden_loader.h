#pragma once
// tests/mermaid/golden_loader.h
//
// Permissive JSON reader for the machine-generated goldens under
// tests/mermaid/golden/. Not a general JSON library. It handles what our
// oracle emits: quoted strings (with \" \\ \n escapes), numbers, booleans,
// null (mapped to a sentinel: -1 for int, NaN for float), objects, arrays.
// Enough for tests, nothing more.

#include "../../src/mermaid/layout_internal.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mermaid {

struct GoldenNode {
    std::string id;
    std::string label;
    float x = 0, y = 0, width = 0, height = 0;
    int rank = -1;
};

struct GoldenEdge {
    std::string from, to;
    std::string label;
    std::vector<std::pair<float, float>> points;
};

struct Golden {
    std::string source;
    std::string rankdir;
    float nodesep = 0, ranksep = 0, edgesep = 0, padding = 0;
    float width = 0, height = 0;
    std::vector<GoldenNode> nodes;
    std::vector<GoldenEdge> edges;
};

namespace goldendetail {

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
            throw std::runtime_error(std::string("golden: expected '") + c + "'");
        ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"')
            throw std::runtime_error("golden: expected string");
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
        if (i >= s.size()) throw std::runtime_error("golden: unterminated string");
        ++i;  // closing quote
        return out;
    }
    // Reads a number or the literal "null". Returns NaN for null.
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
        if (start == i) throw std::runtime_error("golden: expected number");
        return std::strtod(s.c_str() + start, nullptr);
    }
    // Skip a whole JSON value (used to ignore unknown keys).
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
        // number, true, false, null
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']')
            ++i;
    }
};

inline float to_float(double d) {
    return std::isnan(d) ? std::nanf("") : static_cast<float>(d);
}
inline int to_int(double d) {
    return std::isnan(d) ? -1 : static_cast<int>(d);
}

inline std::vector<std::pair<float, float>> parse_points(Cursor& c) {
    std::vector<std::pair<float, float>> out;
    c.expect('[');
    c.skip_ws();
    if (c.peek() == ']') { c.get(); return out; }
    while (true) {
        c.expect('{');
        float x = 0, y = 0;
        while (true) {
            std::string k = c.parse_string();
            c.expect(':');
            double v = c.parse_number_or_null();
            if (k == "x") x = to_float(v);
            else if (k == "y") y = to_float(v);
            c.skip_ws();
            if (c.peek() == ',') { c.get(); continue; }
            break;
        }
        c.expect('}');
        out.emplace_back(x, y);
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect(']');
    return out;
}

inline GoldenNode parse_node(Cursor& c) {
    GoldenNode n;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "id") n.id = c.parse_string();
        else if (k == "label") n.label = c.parse_string();
        else if (k == "x") n.x = to_float(c.parse_number_or_null());
        else if (k == "y") n.y = to_float(c.parse_number_or_null());
        else if (k == "width") n.width = to_float(c.parse_number_or_null());
        else if (k == "height") n.height = to_float(c.parse_number_or_null());
        else if (k == "rank") n.rank = to_int(c.parse_number_or_null());
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return n;
}

inline GoldenEdge parse_edge(Cursor& c) {
    GoldenEdge e;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "from") e.from = c.parse_string();
        else if (k == "to") e.to = c.parse_string();
        else if (k == "label") e.label = c.parse_string();
        else if (k == "points") e.points = parse_points(c);
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return e;
}

}  // namespace goldendetail

inline Golden LoadGolden(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("golden: cannot open " + path);
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    goldendetail::Cursor c(src);
    Golden gold;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "source") gold.source = c.parse_string();
        else if (k == "config") {
            c.expect('{');
            while (true) {
                std::string kk = c.parse_string();
                c.expect(':');
                if (kk == "rankdir") gold.rankdir = c.parse_string();
                else if (kk == "nodesep") gold.nodesep = goldendetail::to_float(c.parse_number_or_null());
                else if (kk == "ranksep") gold.ranksep = goldendetail::to_float(c.parse_number_or_null());
                else if (kk == "edgesep") gold.edgesep = goldendetail::to_float(c.parse_number_or_null());
                else if (kk == "padding") gold.padding = goldendetail::to_float(c.parse_number_or_null());
                else c.skip_value();
                c.skip_ws();
                if (c.peek() == ',') { c.get(); continue; }
                break;
            }
            c.expect('}');
        } else if (k == "graph") {
            c.expect('{');
            while (true) {
                std::string kk = c.parse_string();
                c.expect(':');
                if (kk == "width") gold.width = goldendetail::to_float(c.parse_number_or_null());
                else if (kk == "height") gold.height = goldendetail::to_float(c.parse_number_or_null());
                else c.skip_value();
                c.skip_ws();
                if (c.peek() == ',') { c.get(); continue; }
                break;
            }
            c.expect('}');
        } else if (k == "nodes") {
            c.expect('[');
            c.skip_ws();
            if (c.peek() != ']') {
                while (true) {
                    gold.nodes.push_back(goldendetail::parse_node(c));
                    c.skip_ws();
                    if (c.peek() == ',') { c.get(); continue; }
                    break;
                }
            }
            c.expect(']');
        } else if (k == "edges") {
            c.expect('[');
            c.skip_ws();
            if (c.peek() != ']') {
                while (true) {
                    gold.edges.push_back(goldendetail::parse_edge(c));
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
    return gold;
}

// Build a LayoutGraph mirroring the golden: node sizes and labels are
// injected from the fixture; edges are added in golden order. No layout
// phase runs. The caller decides which phases (MakeAcyclic, AssignRanks,
// ...) to invoke.
inline LayoutGraph LayoutFromGolden(const Golden& gold) {
    LayoutGraph g;
    g.nodes.reserve(gold.nodes.size());
    std::unordered_map<std::string, int> idx;
    for (size_t i = 0; i < gold.nodes.size(); ++i) {
        LayoutNode n;
        n.id = static_cast<int>(i);
        n.label = gold.nodes[i].id;   // store the string id in the label slot
        n.width = gold.nodes[i].width;
        n.height = gold.nodes[i].height;
        g.nodes.push_back(n);
        idx.emplace(gold.nodes[i].id, static_cast<int>(i));
    }
    for (const auto& ge : gold.edges) {
        auto a = idx.find(ge.from), b = idx.find(ge.to);
        if (a == idx.end() || b == idx.end()) continue;
        LayoutEdge e; e.from = a->second; e.to = b->second;
        e.minlen = 1; e.weight = 1; e.label = ge.label;
        g.edges.push_back(e);
    }
    return g;
}

inline int RankOf(const LayoutGraph& g, const std::string& id) {
    for (const auto& n : g.nodes) if (n.label == id) return n.rank;
    return -1;
}
inline float XOf(const LayoutGraph& g, const std::string& id) {
    for (const auto& n : g.nodes) if (n.label == id) return n.x;
    return 0;
}
inline float YOf(const LayoutGraph& g, const std::string& id) {
    for (const auto& n : g.nodes) if (n.label == id) return n.y;
    return 0;
}

}  // namespace mermaid
