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

// Split `s` on whitespace runs.
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

// If `line` looks like an edge (contains `--` or `==`), return true.
// Task 4 will replace this with real edge parsing.
bool LooksLikeEdge(const std::string& line) {
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        char a = line[i], b = line[i + 1];
        if ((a == '-' && b == '-') || (a == '=' && b == '=')) return true;
        if (a == '-' && b == '.') return true;  // -.-> dotted
        if (a == '.' && b == '-') return true;
    }
    return false;
}

// Try to parse a node declaration at the start of `line`.
// Returns true and fills `out` on success; false otherwise.
bool TryParseNode(const std::string& line, FlowNode& out) {
    size_t n = line.size();
    if (n == 0 || !IsIdStart(line[0])) return false;
    size_t i = 1;
    while (i < n && IsIdCont(line[i])) ++i;
    std::string id = line.substr(0, i);

    // Skip whitespace between id and optional bracket.
    while (i < n && IsSpace(line[i])) ++i;

    out.id = id;
    out.label = id;
    out.shape = Shape::Rect;

    if (i >= n) return true;  // bare id

    // Longest-first bracket detection.
    auto starts = [&](const char* p) {
        size_t k = 0;
        while (p[k]) {
            if (i + k >= n || line[i + k] != p[k]) return false;
            ++k;
        }
        return true;
    };
    auto endsWith = [&](const std::string& s, const char* p) {
        size_t k = 0; while (p[k]) ++k;
        if (s.size() < k) return false;
        for (size_t x = 0; x < k; ++x) if (s[s.size() - k + x] != p[x]) return false;
        return true;
    };

    struct Form { const char* open; const char* close; Shape shape; };
    // Order matters: longest opener first.
    const Form forms[] = {
        {"([", "])", Shape::Stadium},
        {"((", "))", Shape::Circle},
        {"[",  "]",  Shape::Rect},
        {"(",  ")",  Shape::Round},
        {"{",  "}",  Shape::Diamond},
    };

    for (const auto& f : forms) {
        if (!starts(f.open)) continue;
        size_t open_len = 0; while (f.open[open_len]) ++open_len;
        size_t close_len = 0; while (f.close[close_len]) ++close_len;
        // Find matching close: last occurrence of close on the line.
        std::string rest = line.substr(i + open_len);
        if (!endsWith(rest, f.close)) return false;
        std::string inner = rest.substr(0, rest.size() - close_len);
        out.label = inner;
        out.shape = f.shape;
        return true;
    }
    // Starts with something else after id: not a node we understand.
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

    // Find the first non-empty, non-comment line for the header.
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
        fc.dir = Dir::TB;  // default
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

    // Body: node declarations. Edges handled in Task 4.
    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;
        if (LooksLikeEdge(line)) continue;  // Task 4

        FlowNode node;
        if (!TryParseNode(line, node)) continue;

        // Skip duplicate ids: keep the first declaration.
        bool dup = false;
        for (const auto& existing : fc.nodes) {
            if (existing.id == node.id) { dup = true; break; }
        }
        if (dup) continue;
        fc.nodes.push_back(std::move(node));
    }

    return fc;
}

}  // namespace mermaid
