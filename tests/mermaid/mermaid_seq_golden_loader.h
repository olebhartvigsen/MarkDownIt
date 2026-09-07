// Sequence golden loader: parses the sequence oracle's JSON (seq1..5.json).
// Permissive cursor JSON like golden_loader.h / mermaid_pie_golden_loader.h.
#pragma once

#include "../../src/mermaid/seq_layout.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mermaid {

struct SeqGoldenCanvas {
    double width = 0, height = 0;
    double startx = 0, starty = 0;
    double vbwidth = 0, vbheight = 0;
};

struct SeqGoldenActor {
    bool bottom = false;
    bool is_lifeline = false;
    std::string name;
    double x = 0, y = 0, w = 0, h = 0;
    double x1 = 0, y1 = 0, y2 = 0;  // lifeline
};

struct SeqGoldenLine {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::string cls;
    std::string marker_end;
    std::string marker_start;
};

struct SeqGoldenMessage {
    std::string text;
    double tx = 0, ty = 0;
    // line form
    bool has_line = false;
    SeqGoldenLine line;
    // self path form
    bool has_path = false;
    std::string path_d;
    std::string path_cls;
};

struct SeqGoldenNote {
    std::string text;
    double tx = 0, ty = 0;
    double rx = 0, ry = 0, rw = 0, rh = 0;
};

struct SeqGoldenActivation {
    double x = 0, y = 0, w = 0, h = 0;
    std::string cls;
};

struct SeqGoldenLoopEnt {
    std::string type;    // top/vert/labelBox/labelText/loopText/loopLineCount
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::string text;
    double x = 0, y = 0;
};

struct SeqGoldenNumber {
    std::string n;
    double x = 0, y = 0;
};

struct SeqGoldenBackground {
    double x = 0, y = 0, w = 0, h = 0;
    std::string fill;
};

struct SeqGolden {
    SeqGoldenCanvas canvas;
    bool has_title = false;
    std::string title;
    double title_x = 0, title_y = 0;
    std::vector<SeqGoldenActor> actors;
    std::vector<SeqGoldenMessage> messages;
    std::vector<SeqGoldenNote> notes;
    std::vector<SeqGoldenActivation> activations;
    std::vector<SeqGoldenLoopEnt> loops;
    std::vector<SeqGoldenNumber> numbers;
    std::vector<SeqGoldenBackground> backgrounds;
};

namespace seqgolddetail {

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
            throw std::runtime_error(std::string("seq golden: expected '") + c + "'");
        ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"')
            throw std::runtime_error("seq golden: expected string");
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
        if (i >= s.size()) throw std::runtime_error("seq golden: unterminated string");
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
        if (start == i) throw std::runtime_error("seq golden: expected number");
        return std::strtod(s.c_str() + start, nullptr);
    }
    void skip_value() {
        skip_ws();
        if (i >= s.size()) return;
        char c = s[i];
        if (c == '"') { parse_string(); return; }
        if (c == '{' || c == '[') {
            int depth = 1; ++i;
            while (i < s.size() && depth > 0) {
                if (s[i] == '"') { parse_string(); continue; }
                if (s[i] == '{' || s[i] == '[') { ++depth; ++i; continue; }
                if (s[i] == '}' || s[i] == ']') { --depth; ++i; continue; }
                ++i;
            }
            return;
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']')
            ++i;
    }
    bool at_key(const std::string& k) {
        skip_ws();
        if (i >= s.size() || s[i] != '"') {
            // tolerate unexpected tokens by skipping the value
            skip_value();
            return false;
        }
        std::string got = parse_string();
        skip_ws();
        if (i < s.size() && s[i] == ':') ++i;
        return got == k;
    }
};

inline double num_or_zero(double d) { return std::isnan(d) ? 0.0 : d; }

inline SeqGoldenActor parse_actor(Cursor& c) {
    SeqGoldenActor a;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "kind") a.is_lifeline = (c.parse_string() == "lifeline");
        else if (k == "name") a.name = c.parse_string();
        else if (k == "bottom") {
            c.skip_ws();
            if (c.peek() == 't') { c.get(); c.get(); c.get(); c.get(); a.bottom = true; }
            else if (c.peek() == 'f') { c.get(); c.get(); c.get(); c.get(); c.get(); a.bottom = false; }
            else { a.bottom = num_or_zero(c.parse_number_or_null()) != 0; }
        }
        else if (k == "x") a.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") a.y = num_or_zero(c.parse_number_or_null());
        else if (k == "w") a.w = num_or_zero(c.parse_number_or_null());
        else if (k == "h") a.h = num_or_zero(c.parse_number_or_null());
        else if (k == "y1") a.y1 = num_or_zero(c.parse_number_or_null());
        else if (k == "y2") a.y2 = num_or_zero(c.parse_number_or_null());
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return a;
}

inline SeqGoldenMessage parse_message(Cursor& c) {
    SeqGoldenMessage m;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "text") m.text = c.parse_string();
        else if (k == "tx") m.tx = num_or_zero(c.parse_number_or_null());
        else if (k == "ty") m.ty = num_or_zero(c.parse_number_or_null());
        else if (k == "line") {
            m.has_line = true;
            c.expect('{');
            while (true) {
                std::string kk = c.parse_string();
                c.expect(':');
                if (kk == "x1") m.line.x1 = num_or_zero(c.parse_number_or_null());
                else if (kk == "y1") m.line.y1 = num_or_zero(c.parse_number_or_null());
                else if (kk == "x2") m.line.x2 = num_or_zero(c.parse_number_or_null());
                else if (kk == "y2") m.line.y2 = num_or_zero(c.parse_number_or_null());
                else if (kk == "cls") m.line.cls = c.parse_string();
                else if (kk == "markerEnd") m.line.marker_end = c.parse_string();
                else if (kk == "markerStart") m.line.marker_start = c.parse_string();
                else c.skip_value();
                c.skip_ws();
                if (c.peek() == ',') { c.get(); continue; }
                break;
            }
            c.expect('}');
        } else if (k == "path") {
            m.has_path = true;
            c.expect('{');
            while (true) {
                std::string kk = c.parse_string();
                c.expect(':');
                if (kk == "d") m.path_d = c.parse_string();
                else if (kk == "cls") m.path_cls = c.parse_string();
                else c.skip_value();
                c.skip_ws();
                if (c.peek() == ',') { c.get(); continue; }
                break;
            }
            c.expect('}');
        } else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return m;
}

inline SeqGoldenNote parse_note(Cursor& c) {
    SeqGoldenNote n;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "text") n.text = c.parse_string();
        else if (k == "tx") n.tx = num_or_zero(c.parse_number_or_null());
        else if (k == "ty") n.ty = num_or_zero(c.parse_number_or_null());
        else if (k == "rx") n.rx = num_or_zero(c.parse_number_or_null());
        else if (k == "ry") n.ry = num_or_zero(c.parse_number_or_null());
        else if (k == "rw") n.rw = num_or_zero(c.parse_number_or_null());
        else if (k == "rh") n.rh = num_or_zero(c.parse_number_or_null());
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return n;
}

inline SeqGoldenActivation parse_activation(Cursor& c) {
    SeqGoldenActivation a;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "x") a.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") a.y = num_or_zero(c.parse_number_or_null());
        else if (k == "w") a.w = num_or_zero(c.parse_number_or_null());
        else if (k == "h") a.h = num_or_zero(c.parse_number_or_null());
        else if (k == "cls") a.cls = c.parse_string();
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return a;
}

inline SeqGoldenLoopEnt parse_loop_ent(Cursor& c) {
    SeqGoldenLoopEnt e;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "loopLineCount") { e.type = "count"; e.x1 = num_or_zero(c.parse_number_or_null()); }
        else if (k == "top" || k == "vert" || k == "horiz") {
            e.type = k;
            c.skip_ws();
            if (c.peek() == 't') {          // literal `true`
                c.get(); c.get(); c.get(); c.get();
            } else if (c.peek() >= '0' && c.peek() <= '9') {
                e.y1 = num_or_zero(c.parse_number_or_null());
            } else {
                c.skip_value();
            }
        }
        else if (k == "x1") e.x1 = num_or_zero(c.parse_number_or_null());
        else if (k == "y1") e.y1 = num_or_zero(c.parse_number_or_null());
        else if (k == "x2") e.x2 = num_or_zero(c.parse_number_or_null());
        else if (k == "y2") e.y2 = num_or_zero(c.parse_number_or_null());
        else if (k == "labelBox") { e.type = "labelBox"; e.text = c.parse_string(); }
        else if (k == "labelText") { e.type = "labelText"; e.text = c.parse_string(); }
        else if (k == "x") e.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") e.y = num_or_zero(c.parse_number_or_null());
        else if (k == "loopText") { e.type = "loopText"; e.text = c.parse_string(); }
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return e;
}

inline SeqGoldenNumber parse_number(Cursor& c) {
    SeqGoldenNumber n;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "n") n.n = c.parse_string();
        else if (k == "x") n.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") n.y = num_or_zero(c.parse_number_or_null());
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return n;
}

inline SeqGoldenBackground parse_background(Cursor& c) {
    SeqGoldenBackground b;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "x") b.x = num_or_zero(c.parse_number_or_null());
        else if (k == "y") b.y = num_or_zero(c.parse_number_or_null());
        else if (k == "w") b.w = num_or_zero(c.parse_number_or_null());
        else if (k == "h") b.h = num_or_zero(c.parse_number_or_null());
        else if (k == "fill") b.fill = c.parse_string();
        else c.skip_value();
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect('}');
    return b;
}

template <typename T, typename F>
inline void parse_array(Cursor& c, std::vector<T>& out, F fn) {
    c.expect('[');
    c.skip_ws();
    if (c.peek() == ']') { c.get(); return; }
    while (true) {
        out.push_back(fn(c));
        c.skip_ws();
        if (c.peek() == ',') { c.get(); continue; }
        break;
    }
    c.expect(']');
}

}  // namespace seqgolddetail

inline SeqGolden LoadSeqGolden(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("seq golden: cannot open " + path);
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    seqgolddetail::Cursor c(src);
    SeqGolden g;
    c.expect('{');
    while (true) {
        std::string k = c.parse_string();
        c.expect(':');
        if (k == "canvas") {
            c.expect('{');
            while (true) {
                std::string kk = c.parse_string();
                c.expect(':');
                if (kk == "width") g.canvas.width = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else if (kk == "height") g.canvas.height = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else if (kk == "startx") g.canvas.startx = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else if (kk == "starty") g.canvas.starty = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else if (kk == "vbwidth") g.canvas.vbwidth = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else if (kk == "vbheight") g.canvas.vbheight = seqgolddetail::num_or_zero(c.parse_number_or_null());
                else c.skip_value();
                c.skip_ws();
                if (c.peek() == ',') { c.get(); continue; }
                break;
            }
            c.expect('}');
        }
        else if (k == "title") {
            c.skip_ws();
            if (c.peek() == '{') {
                c.get();
                while (true) {
                    std::string kk = c.parse_string();
                    c.expect(':');
                    if (kk == "text") { g.has_title = true; g.title = c.parse_string(); }
                    else if (kk == "x") g.title_x = seqgolddetail::num_or_zero(c.parse_number_or_null());
                    else if (kk == "y") g.title_y = seqgolddetail::num_or_zero(c.parse_number_or_null());
                    else c.skip_value();
                    c.skip_ws();
                    if (c.peek() == ',') { c.get(); continue; }
                    break;
                }
                c.expect('}');
            } else if (c.peek() == '"') {
                g.has_title = true;
                g.title = c.parse_string();
            } else {
                c.skip_value();
            }
        } else if (k == "actors") {
            seqgolddetail::parse_array(c, g.actors, seqgolddetail::parse_actor);
        } else if (k == "messages") {
            seqgolddetail::parse_array(c, g.messages, seqgolddetail::parse_message);
        } else if (k == "notes") {
            seqgolddetail::parse_array(c, g.notes, seqgolddetail::parse_note);
        } else if (k == "activations") {
            seqgolddetail::parse_array(c, g.activations, seqgolddetail::parse_activation);
        } else if (k == "loops") {
            seqgolddetail::parse_array(c, g.loops, seqgolddetail::parse_loop_ent);
        } else if (k == "sequenceNumbers") {
            seqgolddetail::parse_array(c, g.numbers, seqgolddetail::parse_number);
        } else if (k == "backgrounds") {
            seqgolddetail::parse_array(c, g.backgrounds, seqgolddetail::parse_background);
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
