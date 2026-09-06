#include "parse.h"

#include <string>
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

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && IsSpace(s[i])) ++i;
        size_t j = i;
        while (j < n && !IsSpace(s[j])) ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool IsIdStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool IsIdCont(char c) {
    return IsIdStart(c) || (c >= '0' && c <= '9');
}

void SkipWs(const std::string& line, size_t& pos) {
    while (pos < line.size() && IsSpace(line[pos])) ++pos;
}

bool TryParseNodeAt(const std::string& line, size_t& pos, FlowNode& out) {
    size_t n = line.size();
    if (pos >= n || !IsIdStart(line[pos])) return false;
    size_t s = pos;
    size_t i = pos + 1;
    while (i < n && IsIdCont(line[i])) ++i;
    std::string id = line.substr(s, i - s);

    out.id = id;
    out.label = id;
    out.shape = Shape::Rect;

    if (i >= n) { pos = i; return true; }

    struct Form { const char* open; const char* close; Shape shape; };
    const Form forms[] = {
        {"([", "])", Shape::Stadium},
        {"((", "))", Shape::Circle},
        {"[",  "]",  Shape::Rect},
        {"(",  ")",  Shape::Round},
        {"{",  "}",  Shape::Diamond},
    };

    auto startsAt = [&](size_t p, const char* q) {
        size_t k = 0;
        while (q[k]) {
            if (p + k >= n || line[p + k] != q[k]) return false;
            ++k;
        }
        return true;
    };

    for (const auto& f : forms) {
        if (!startsAt(i, f.open)) continue;
        size_t open_len = 0; while (f.open[open_len]) ++open_len;
        size_t close_len = 0; while (f.close[close_len]) ++close_len;
        size_t j = i + open_len;
        size_t close_pos = std::string::npos;
        while (j + close_len <= n) {
            bool match = true;
            for (size_t k = 0; k < close_len; ++k) {
                if (line[j + k] != f.close[k]) { match = false; break; }
            }
            if (match) { close_pos = j; break; }
            ++j;
        }
        if (close_pos == std::string::npos) return false;
        out.label = line.substr(i + open_len, close_pos - (i + open_len));
        out.shape = f.shape;
        pos = close_pos + close_len;
        return true;
    }

    pos = i;
    return true;
}

struct EdgeOp {
    LineStyle style = LineStyle::Solid;
    Head head = Head::Arrow;
    std::string label;
};

bool MatchEdgeOp(const std::string& line, size_t& pos, EdgeOp& op) {
    size_t n = line.size();
    if (pos >= n) return false;

    auto startsAt = [&](size_t p, const char* q) {
        size_t k = 0;
        while (q[k]) {
            if (p + k >= n || line[p + k] != q[k]) return false;
            ++k;
        }
        return true;
    };

    if (startsAt(pos, "==>")) {
        op.style = LineStyle::Thick;
        op.head = Head::Arrow;
        op.label.clear();
        pos += 3;
        return true;
    }

    if (startsAt(pos, "-.->")) {
        op.style = LineStyle::Dotted;
        op.head = Head::Arrow;
        op.label.clear();
        pos += 4;
        return true;
    }

    if (startsAt(pos, "--")) {
        if (startsAt(pos, "-->|")) {
            size_t start = pos + 4;
            size_t end = line.find('|', start);
            if (end == std::string::npos) return false;
            op.style = LineStyle::Solid;
            op.head = Head::Arrow;
            op.label = line.substr(start, end - start);
            pos = end + 1;
            return true;
        }
        if (startsAt(pos, "-->")) {
            op.style = LineStyle::Solid;
            op.head = Head::Arrow;
            op.label.clear();
            pos += 3;
            return true;
        }
        if (pos + 2 < n && line[pos + 2] == ' ') {
            size_t p = pos + 2;
            while (p < n && line[p] == ' ') ++p;
            size_t label_start = p;
            size_t tail = std::string::npos;
            bool tail_arrow = true;
            for (size_t q = p; q + 3 < n; ++q) {
                if (line[q] == ' ' && line[q + 1] == '-' && line[q + 2] == '-') {
                    if (line[q + 3] == '>') { tail = q; tail_arrow = true; break; }
                    if (line[q + 3] == '-') { tail = q; tail_arrow = false; break; }
                }
            }
            if (tail != std::string::npos) {
                std::string label = line.substr(label_start, tail - label_start);
                while (!label.empty() && label.back() == ' ') label.pop_back();
                op.style = LineStyle::Solid;
                op.head = tail_arrow ? Head::Arrow : Head::None;
                op.label = label;
                pos = tail + 4;
                return true;
            }
        }
        if (startsAt(pos, "---")) {
            op.style = LineStyle::Solid;
            op.head = Head::None;
            op.label.clear();
            pos += 3;
            return true;
        }
    }

    return false;
}

int FindNodeIndex(const Flowchart& fc, const std::string& id) {
    for (size_t k = 0; k < fc.nodes.size(); ++k) {
        if (fc.nodes[k].id == id) return (int)k;
    }
    return -1;
}

// Insert node if new; then register the (possibly-existing) node with every
// currently-open subgraph on the stack, deduping.
int UpsertNode(Flowchart& fc, const FlowNode& decl,
               const std::vector<int>& sg_stack) {
    int idx = FindNodeIndex(fc, decl.id);
    if (idx < 0) {
        fc.nodes.push_back(decl);
        idx = (int)fc.nodes.size() - 1;
    }
    for (int sgi : sg_stack) {
        auto& v = fc.subgraphs[sgi].node_indices;
        bool seen = false;
        for (int k : v) if (k == idx) { seen = true; break; }
        if (!seen) v.push_back(idx);
    }
    return idx;
}

void ParseBodyLine(const std::string& line, Flowchart& fc,
                   const std::vector<int>& sg_stack) {
    size_t pos = 0;
    SkipWs(line, pos);

    FlowNode from_decl;
    if (!TryParseNodeAt(line, pos, from_decl)) return;
    int from_idx = UpsertNode(fc, from_decl, sg_stack);

    while (pos < line.size()) {
        SkipWs(line, pos);
        if (pos >= line.size()) break;

        EdgeOp op;
        if (!MatchEdgeOp(line, pos, op)) return;
        SkipWs(line, pos);

        FlowNode to_decl;
        if (!TryParseNodeAt(line, pos, to_decl)) return;
        int to_idx = UpsertNode(fc, to_decl, sg_stack);

        FlowEdge e;
        e.from = from_idx;
        e.to = to_idx;
        e.style = op.style;
        e.head = op.head;
        e.label = op.label;
        fc.edges.push_back(std::move(e));

        from_idx = to_idx;
    }
}

// Try to match `subgraph <id>[ <title>]` on a trimmed line. Title may be
// bracketed [Title] or plain text after the id. Returns true and fills sg.
bool TryParseSubgraphHeader(const std::string& line, Subgraph& sg) {
    // line already trimmed; must begin with keyword "subgraph"
    const char* kw = "subgraph";
    size_t klen = 8;
    if (line.size() < klen) return false;
    for (size_t i = 0; i < klen; ++i) if (line[i] != kw[i]) return false;
    if (line.size() > klen && !IsSpace(line[klen])) return false;
    size_t pos = klen;
    SkipWs(line, pos);
    if (pos >= line.size()) return false;
    // id: identifier
    size_t s = pos;
    if (!IsIdStart(line[pos])) {
        // Allow a title-only subgraph: "subgraph MyTitle" where MyTitle is
        // both id and title (single token).
        return false;
    }
    size_t i = pos + 1;
    while (i < line.size() && IsIdCont(line[i])) ++i;
    sg.id = line.substr(s, i - s);
    sg.title = sg.id;
    pos = i;
    SkipWs(line, pos);
    if (pos < line.size()) {
        // Optional [Title] or bare rest-of-line as title.
        if (line[pos] == '[') {
            size_t end = line.find(']', pos + 1);
            if (end != std::string::npos) {
                sg.title = line.substr(pos + 1, end - pos - 1);
            }
        } else {
            sg.title = line.substr(pos);
        }
    }
    return true;
}

bool IsEndKeyword(const std::string& line) {
    return line == "end" || line == "END" || line == "End";
}

bool TryParseDirection(const std::string& line, Dir& out) {
    // "direction TB|BT|LR|RL"
    const char* kw = "direction";
    size_t klen = 9;
    if (line.size() < klen + 2) return false;
    for (size_t i = 0; i < klen; ++i) if (line[i] != kw[i]) return false;
    if (!IsSpace(line[klen])) return false;
    size_t pos = klen;
    SkipWs(line, pos);
    std::string d = line.substr(pos);
    if (d == "TB" || d == "TD") { out = Dir::TB; return true; }
    if (d == "BT") { out = Dir::BT; return true; }
    if (d == "LR") { out = Dir::LR; return true; }
    if (d == "RL") { out = Dir::RL; return true; }
    return false;
}

}  // namespace

Flowchart ParseFlowchart(std::string_view src) {
    Flowchart fc;

    size_t i = 0, n = src.size();

    auto next_line = [&](std::string& out) -> bool {
        if (i >= n) return false;
        size_t j = i;
        while (j < n && src[j] != '\n') ++j;
        out = Trim(src.substr(i, j - i));
        i = (j < n) ? j + 1 : j;
        return true;
    };

    std::string header;
    std::string line;
    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;
        header = line;
        break;
    }

    if (header.empty()) {
        fc.error = "not a flowchart diagram";
        return fc;
    }

    auto tokens = Split(header);
    if (tokens.empty() || (tokens[0] != "flowchart" && tokens[0] != "graph")) {
        fc.error = "not a flowchart diagram";
        return fc;
    }

    if (tokens.size() == 1) {
        fc.dir = Dir::TB;
    } else if (tokens.size() == 2) {
        const std::string& d = tokens[1];
        if (d == "TB" || d == "TD") fc.dir = Dir::TB;
        else if (d == "BT")         fc.dir = Dir::BT;
        else if (d == "LR")         fc.dir = Dir::LR;
        else if (d == "RL")         fc.dir = Dir::RL;
        else { fc.error = "unknown flowchart direction"; return fc; }
    } else {
        fc.error = "malformed flowchart header";
        return fc;
    }

    // Stack of open subgraph indices (into fc.subgraphs).
    std::vector<int> sg_stack;

    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;

        Subgraph sg_hdr;
        if (TryParseSubgraphHeader(line, sg_hdr)) {
            sg_hdr.direction = fc.dir;
            fc.subgraphs.push_back(sg_hdr);
            int new_idx = (int)fc.subgraphs.size() - 1;
            if (!sg_stack.empty()) {
                fc.subgraphs[sg_stack.back()].child_subgraphs.push_back(new_idx);
            }
            sg_stack.push_back(new_idx);
            continue;
        }
        if (IsEndKeyword(line)) {
            if (!sg_stack.empty()) sg_stack.pop_back();
            continue;
        }
        Dir dir_override;
        if (!sg_stack.empty() && TryParseDirection(line, dir_override)) {
            fc.subgraphs[sg_stack.back()].direction = dir_override;
            fc.subgraphs[sg_stack.back()].has_direction = true;
            continue;
        }
        ParseBodyLine(line, fc, sg_stack);
    }

    return fc;
}

}  // namespace mermaid
