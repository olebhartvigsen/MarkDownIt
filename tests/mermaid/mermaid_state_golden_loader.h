// State golden loader: parse the state_oracle.mjs JSON dumps (stt*.json).
// Pattern copied from mermaid_class_golden_loader.h.
#pragma once
#include <cstdlib>
#include <string>
#include <vector>

namespace mermaid {

struct StateGoldenNode {
    std::string id;
    std::string kind;
    double cx = 0, cy = 0, w = 0, h = 0, r = 0;
};

struct StateGoldenEdgePoint { double x, y; };

struct StateGoldenEdge {
    std::string id, from, to, d;
    std::vector<StateGoldenEdgePoint> points;
};

struct StateGoldenLabel {
    std::string kind, text;
    double x = 0, y = 0, w = 0, h = 0;
};

struct StateGoldenCanvas { double width = 0, height = 0, startx = 0, starty = 0, vbwidth = 0, vbheight = 0; };

struct StateGolden {
    std::vector<StateGoldenNode> nodes;
    std::vector<StateGoldenEdge> edges;
    std::vector<StateGoldenLabel> labels;
    StateGoldenCanvas canvas;
};

namespace {

// Minimal incremental JSON cursor: extracts one value for a key at a time.
class StateJsonCursor {
public:
    explicit StateJsonCursor(const std::string& src) : src_(src) {}

    // Find "key": within the current position; returns false when absent.
    bool Find(const std::string& key) {
        std::string pat = "\"" + key + "\"";
        size_t p = src_.find(pat, from_);
        if (p == std::string::npos) return false;
        from_ = p + pat.size();
        return true;
    }

    std::string Str() {
        size_t p = src_.find('"', from_);
        if (p == std::string::npos) return "";
        size_t q = src_.find('"', p + 1);
        if (q == std::string::npos) return "";
        from_ = q + 1;
        return src_.substr(p + 1, q - p - 1);
    }

    double Num() {
        size_t p = src_.find_first_of("-0123456789.", from_);
        if (p == std::string::npos) return 0;
        const char* s = src_.c_str() + p;
        double v = std::strtod(s, nullptr);
        // advance past the number
        const char* t = s;
        while (*t == '-' || *t == '+' || *t == '.' || (*t >= '0' && *t <= '9') || *t == 'e' || *t == 'E') ++t;
        from_ = p + static_cast<size_t>(t - s);
        return v;
    }

    size_t pos() const { return from_; }
    void set_pos(size_t p) { from_ = p; }

private:
    const std::string& src_;
    size_t from_ = 0;
};

}  // namespace

inline StateGolden LoadStateGolden(const std::string& path) {
    StateGolden g;
    std::string src;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "cannot open golden %s\n", path.c_str());
        return g;
    }
    char buf[262144];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    src.assign(buf, n);

    StateJsonCursor c(src);


    // top level: "nodes" array
    if (c.Find("nodes")) {

        size_t arrStart = src.find('[', c.pos());
        // walk objects
        size_t p = arrStart + 1;
        while (p < src.size() && src[p] != ']') {
            size_t ob = src.find('{', p);
            if (ob == std::string::npos || ob > src.find(']', p)) break;
            size_t cb = src.find('}', ob);
            std::string obj = src.substr(ob, cb - ob + 1);
            StateJsonCursor o(obj);
            o.set_pos(0);
            StateGoldenNode n;
            if (o.Find("id")) n.id = o.Str();
            if (o.Find("kind")) n.kind = o.Str();
            if (o.Find("cx")) n.cx = o.Num();
            if (o.Find("cy")) n.cy = o.Num();
            if (o.Find("w")) n.w = o.Num();
            if (o.Find("h")) n.h = o.Num();
            if (o.Find("r")) n.r = o.Num();
            g.nodes.push_back(n);
            p = cb + 1;
        }
    }

    if (c.Find("edges")) {

        // Sequential field scan: the individual fields never occur inside
        // the points arrays, so advance cursor past each edge object by
        // skipping to the closing bracket of "points" when present.
        for (int ecount = 0; ecount < 64; ++ecount) {
            // Each edge object starts with "id"
            if (!c.Find("id")) break;
            StateGoldenEdge e;
            e.id = c.Str();
            if (c.Find("from")) e.from = c.Str();
            if (c.Find("to")) e.to = c.Str();
            if (c.Find("d")) e.d = c.Str();
            if (c.Find("points")) {
                // skip the points array (objects contain {x,y} pairs)
                size_t ps = src.find('[', c.pos());
                size_t pe = ps == std::string::npos ? c.pos() : src.find(']', ps);
                c.set_pos(pe == std::string::npos ? c.pos() : pe + 1);
            }
            g.edges.push_back(e);
        }
    }

    if (c.Find("edge_labels")) {

        size_t arrStart = src.find('[', c.pos());
        size_t p = arrStart + 1;
        while (p < src.size() && src[p] != ']') {
            size_t ob = src.find('{', p);
            if (ob == std::string::npos || ob > src.find(']', p)) break;
            size_t cb = src.find('}', ob);
            std::string obj = src.substr(ob, cb - ob + 1);
            StateJsonCursor o(obj);
            o.set_pos(0);
            StateGoldenLabel l;
            if (o.Find("kind")) l.kind = o.Str();
            if (o.Find("text")) l.text = o.Str();
            if (o.Find("x")) l.x = o.Num();
            if (o.Find("y")) l.y = o.Num();
            if (o.Find("w")) l.w = o.Num();
            if (o.Find("h")) l.h = o.Num();
            g.labels.push_back(l);
            p = cb + 1;
        }
    }

    // Canvas comes FIRST in the oracle JSON, so parse it before the arrays.
    c.set_pos(0);
    if (c.Find("canvas")) {
        size_t ob = src.find('{', c.pos());
        size_t cb = src.find('}', ob);
        std::string obj = src.substr(ob, cb - ob + 1);
        StateJsonCursor o(obj);
        o.set_pos(0);
        if (o.Find("width")) g.canvas.width = o.Num();
        if (o.Find("height")) g.canvas.height = o.Num();
        if (o.Find("startx")) g.canvas.startx = o.Num();
        if (o.Find("starty")) g.canvas.starty = o.Num();
        if (o.Find("vbwidth")) g.canvas.vbwidth = o.Num();
        if (o.Find("vbheight")) g.canvas.vbheight = o.Num();
        c.set_pos(cb + 1);
    }
    return g;
}

}  // namespace mermaid