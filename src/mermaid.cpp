#include "mermaid.h"

#include <cctype>
#include <cstring>
#include <unordered_map>
#include <sstream>
#include <algorithm>

namespace mermaid {

// --- Helpers ---

static std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

static std::string ToLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Split source into lines, strip comments (lines starting with %%).
static std::vector<std::string> SplitLines(const std::string& src) {
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (src[i] != '\r') {
            line += src[i];
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

static bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

// --- Node/edge parsing helpers for flowchart ---

// Get or create a node by id, returns index into d.nodes.
static int GetOrCreateNode(Diagram& d, std::unordered_map<std::string, int>& idMap,
                           const std::string& id) {
    auto it = idMap.find(id);
    if (it != idMap.end()) return it->second;
    GraphNode n;
    n.id = id;
    n.label = id;  // default label is the id itself
    n.shape = NodeShape::Rect;
    int idx = static_cast<int>(d.nodes.size());
    d.nodes.push_back(n);
    idMap[id] = idx;
    return idx;
}

// Parse a node definition like A[Label], B(Label), C{Label}, D((Label)), E([Label])
// Returns the id and sets label/shape if a definition is found.
// Edge targets like B in "A --> B" are plain ids with no shape.

static bool IsIdChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Parse an identifier from pos, advancing pos.
static std::string ParseId(const std::string& s, size_t& pos) {
    size_t start = pos;
    while (pos < s.size() && IsIdChar(s[pos])) pos++;
    return s.substr(start, pos - start);
}

// Parse a quoted string from pos (pos points at opening quote). Returns content.
static std::string ParseQuoted(const std::string& s, size_t& pos) {
    // pos points at the quote character
    pos++;  // skip opening quote
    std::string result;
    while (pos < s.size() && s[pos] != '"' && s[pos] != '\'') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            pos++;
        }
        result += s[pos];
        pos++;
    }
    if (pos < s.size()) pos++;  // skip closing quote
    return result;
}

// Find matching closing bracket, respecting quoted strings inside.
static size_t FindMatchingBracket(const std::string& s, size_t pos, char open, char close) {
    // pos points at the opening bracket
    size_t p = pos + 1;
    bool inQuotes = false;
    char quoteChar = 0;
    while (p < s.size()) {
        if (inQuotes) {
            if (s[p] == '\\' && p + 1 < s.size()) {
                p += 2;
                continue;
            }
            if (s[p] == quoteChar) {
                inQuotes = false;
            }
        } else {
            if (s[p] == '"' || s[p] == '\'') {
                inQuotes = true;
                quoteChar = s[p];
            } else if (s[p] == close) {
                return p;
            }
        }
        p++;
    }
    return std::string::npos;
}

// Parse a bracketed label: [text], (text), {text}, ((text)), ([text])
// ctx is the opening bracket char. Returns label and sets shape.
// Returns false if no match.
static bool ParseBracketedLabel(const std::string& s, size_t& pos,
                                std::string& label, NodeShape& shape) {
    if (pos >= s.size()) return false;
    char c = s[pos];
    if (c == '[') {
        // Check for stadium: [(
        if (pos + 1 < s.size() && s[pos + 1] == '[') {
            // Double bracket [[ ... ]] is not standard; treat as rect
        }
        // Check for stadium: ([ ... ])
        if (pos + 1 < s.size() && s[pos + 1] == '(') {
            // Stadium shape ([text])
            size_t end = s.find("])", pos + 2);
            if (end != std::string::npos) {
                label = s.substr(pos + 2, end - pos - 2);
                // Trim outer parens content
                // Actually for ([text]) the content is between ([ and ])
                label = Trim(label);
                shape = NodeShape::Stadium;
                pos = end + 2;
                return true;
            }
        }
        size_t end = FindMatchingBracket(s, pos, '[', ']');
        if (end != std::string::npos) {
            std::string raw = s.substr(pos + 1, end - pos - 1);
            // Check for quoted
            if (!raw.empty() && (raw[0] == '"' || raw[0] == '\'')) {
                size_t qp = 0;
                label = ParseQuoted(raw, qp);
            } else {
                label = raw;
            }
            shape = NodeShape::Rect;
            pos = end + 1;
            return true;
        }
    } else if (c == '(') {
        // Check for circle: ((text))
        if (pos + 1 < s.size() && s[pos + 1] == '(') {
            size_t end = s.find("))", pos + 2);
            if (end != std::string::npos) {
                label = s.substr(pos + 2, end - pos - 2);
                label = Trim(label);
                shape = NodeShape::Circle;
                pos = end + 2;
                return true;
            }
        }
        size_t end = FindMatchingBracket(s, pos, '(', ')');
        if (end != std::string::npos) {
            std::string raw = s.substr(pos + 1, end - pos - 1);
            if (!raw.empty() && (raw[0] == '"' || raw[0] == '\'')) {
                size_t qp = 0;
                label = ParseQuoted(raw, qp);
            } else {
                label = raw;
            }
            shape = NodeShape::RoundRect;
            pos = end + 1;
            return true;
        }
    } else if (c == '{') {
        size_t end = s.find('}', pos + 1);
        if (end != std::string::npos) {
            std::string raw = s.substr(pos + 1, end - pos - 1);
            if (!raw.empty() && (raw[0] == '"' || raw[0] == '\'')) {
                size_t qp = 0;
                label = ParseQuoted(raw, qp);
            } else {
                label = raw;
            }
            shape = NodeShape::Diamond;
            pos = end + 1;
            return true;
        }
    }
    return false;
}

// Edge style parsing. Advances pos past the edge operator.
// Returns true if an edge was found, sets style/head/label.
struct EdgeToken {
    EdgeStyle style = EdgeStyle::Solid;
    ArrowHead head = ArrowHead::Arrow;
    std::string label;
    bool found = false;
};

static EdgeToken ParseEdge(const std::string& s, size_t& pos) {
    EdgeToken tok;
    // Skip spaces
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (pos >= s.size()) return tok;

    // Look for edge operators: -->, ---, -.->, ==>, -->> , --, -.
    // Also with label: -->|text| or -- text -->

    // Check for thick: ==>
    if (pos + 2 < s.size() && s[pos] == '=' && s[pos + 1] == '=' &&
        (s[pos + 2] == '>' || (pos + 3 < s.size() && s[pos + 2] == '=' && s[pos + 3] == '>'))) {
        tok.style = EdgeStyle::Thick;
        tok.head = ArrowHead::Arrow;
        if (s[pos + 2] == '>') { pos += 3; }
        else { pos += 4; }
        tok.found = true;
        return tok;
    }

    // Check for == (thick without arrow, rare)
    if (pos + 1 < s.size() && s[pos] == '=' && s[pos + 1] == '=') {
        tok.style = EdgeStyle::Thick;
        tok.head = ArrowHead::None;
        pos += 2;
        tok.found = true;
        return tok;
    }

    // All other edges start with -
    if (s[pos] != '-') return tok;

    // --- (solid, no arrow) or --> (solid arrow) or ---> (solid arrow, extra dashes)
    // -. (dotted)
    if (pos + 1 < s.size() && s[pos + 1] == '.') {
        // Dotted edge: -.-> or -.-
        // Skip dots and dashes
        size_t p = pos;
        while (p < s.size() && (s[p] == '-' || s[p] == '.')) p++;
        // Now look for > or skip
        tok.style = EdgeStyle::Dotted;
        if (p < s.size() && s[p] == '>') {
            tok.head = ArrowHead::Arrow;
            p++;
        } else {
            tok.head = ArrowHead::None;
        }
        // Look for label: -.->|text|
        if (p < s.size() && s[p] == '|') {
            size_t end = s.find('|', p + 1);
            if (end != std::string::npos) {
                tok.label = s.substr(p + 1, end - p - 1);
                p = end + 1;
            }
        }
        pos = p;
        tok.found = true;
        return tok;
    }

    // --- or -->
    if (pos + 1 < s.size() && s[pos + 1] == '-') {
        size_t p = pos;
        while (p < s.size() && s[p] == '-') p++;
        tok.style = EdgeStyle::Solid;
        if (p < s.size() && s[p] == '>') {
            tok.head = ArrowHead::Arrow;
            p++;
            // Look for label: -->|text|
            if (p < s.size() && s[p] == '|') {
                size_t end = s.find('|', p + 1);
                if (end != std::string::npos) {
                    tok.label = s.substr(p + 1, end - p - 1);
                    p = end + 1;
                }
            } else {
                // Look for label in form: -- text -->
                // This was already consumed as dashes, so check remaining
            }
        } else {
            tok.head = ArrowHead::None;
            // Could be -- text -- pattern
            // Check if there's text then more dashes
            size_t textStart = p;
            while (textStart < s.size() && std::isspace(static_cast<unsigned char>(s[textStart])))
                textStart++;
            if (textStart < s.size() && s[textStart] != '-') {
                // Read until next --
                size_t nextDash = s.find("--", textStart);
                if (nextDash != std::string::npos) {
                    std::string txt = s.substr(textStart, nextDash - textStart);
                    tok.label = Trim(txt);
                    p = nextDash + 2;
                    if (p < s.size() && s[p] == '>') p++;
                }
            }
        }
        pos = p;
        tok.found = true;
        return tok;
    }

    // Single dash not an edge operator in mermaid
    return tok;
}

// Parse a flowchart content line: A[Label] -->|text| B[Label]
// or A --> B --> C (chained)
static void ParseFlowchartLine(Diagram& d,
                              std::unordered_map<std::string, int>& idMap,
                              const std::string& line) {
    size_t pos = 0;
    // Skip leading spaces
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) pos++;
    if (pos >= line.size()) return;

    // Parse first node
    std::string id = ParseId(line, pos);
    if (id.empty()) return;
    int firstIdx = GetOrCreateNode(d, idMap, id);

    // Check for bracketed label on first node
    std::string label;
    NodeShape shape;
    if (pos < line.size() && (line[pos] == '[' || line[pos] == '(' || line[pos] == '{')) {
        if (ParseBracketedLabel(line, pos, label, shape)) {
            d.nodes[firstIdx].label = label;
            d.nodes[firstIdx].shape = shape;
        }
    }

    // Now parse edges and target nodes in a loop
    while (pos < line.size()) {
        EdgeToken tok = ParseEdge(line, pos);
        if (!tok.found) break;

        // Skip spaces
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) pos++;
        if (pos >= line.size()) break;

        // Parse target node
        std::string targetId = ParseId(line, pos);
        if (targetId.empty()) break;
        int targetIdx = GetOrCreateNode(d, idMap, targetId);

        // Check for bracketed label on target
        if (pos < line.size() && (line[pos] == '[' || line[pos] == '(' || line[pos] == '{')) {
            std::string tlabel;
            NodeShape tshape;
            if (ParseBracketedLabel(line, pos, tlabel, tshape)) {
                d.nodes[targetIdx].label = tlabel;
                d.nodes[targetIdx].shape = tshape;
            }
        }

        GraphEdge e;
        e.from = firstIdx;
        e.to = targetIdx;
        e.style = tok.style;
        e.head = tok.head;
        e.label = tok.label;
        d.edges.push_back(e);

        firstIdx = targetIdx;
    }
}

// Parse a sequence diagram line.
static void ParseSeqLine(Diagram& d,
                         std::unordered_map<std::string, int>& idMap,
                         const std::string& line) {
    std::string t = Trim(line);
    if (t.empty()) return;

    // participant / actor
    if (StartsWith(ToLower(t), "participant ") || StartsWith(ToLower(t), "actor ")) {
        size_t sp = t.find(' ');
        std::string name = Trim(t.substr(sp + 1));
        if (!name.empty()) {
            GetOrCreateNode(d, idMap, name);
        }
        return;
    }

    // Message: A->>B: text or A-->>B: text or A->B: text or A-x B: text
    // Find the arrow
    // Look for ->> , -->>, ->, -x
    size_t arrowPos = std::string::npos;
    EdgeStyle style = EdgeStyle::Solid;

    // Check for -->> (dotted)
    arrowPos = t.find("-->>");
    if (arrowPos != std::string::npos) {
        style = EdgeStyle::Dotted;
    } else {
        arrowPos = t.find("->>");
        if (arrowPos != std::string::npos) {
            style = EdgeStyle::Solid;
        } else {
            arrowPos = t.find("-->");
            if (arrowPos != std::string::npos) {
                style = EdgeStyle::Dotted;
            } else {
                arrowPos = t.find("->");
                if (arrowPos != std::string::npos) {
                    style = EdgeStyle::Solid;
                } else {
                    arrowPos = t.find("-x");
                    if (arrowPos != std::string::npos) {
                        style = EdgeStyle::Solid;  // could add Cross head later
                    }
                }
            }
        }
    }

    if (arrowPos == std::string::npos) return;

    std::string fromId = Trim(t.substr(0, arrowPos));
    // advance past arrow
    size_t afterArrow = arrowPos;
    // Find the end of the arrow operator
    while (afterArrow < t.size() && (t[afterArrow] == '-' || t[afterArrow] == '>' || t[afterArrow] == 'x'))
        afterArrow++;

    // Find the colon that separates target from message
    size_t colonPos = t.find(':', afterArrow);
    std::string toId;
    std::string msg;
    if (colonPos != std::string::npos) {
        toId = Trim(t.substr(afterArrow, colonPos - afterArrow));
        msg = Trim(t.substr(colonPos + 1));
    } else {
        toId = Trim(t.substr(afterArrow));
    }

    if (fromId.empty() || toId.empty()) return;

    int fromIdx = GetOrCreateNode(d, idMap, fromId);
    int toIdx = GetOrCreateNode(d, idMap, toId);

    SeqMessage m;
    m.from = fromIdx;
    m.to = toIdx;
    m.label = msg;
    m.style = style;
    d.messages.push_back(m);
}

// --- Main parser ---

Diagram Parse(const std::string& src) {
    Diagram d;
    std::vector<std::string> lines = SplitLines(src);

    // Find first non-empty, non-comment line
    size_t firstLine = 0;
    while (firstLine < lines.size()) {
        std::string trimmed = Trim(lines[firstLine]);
        if (trimmed.empty() || StartsWith(trimmed, "%%")) {
            firstLine++;
            continue;
        }
        break;
    }

    if (firstLine >= lines.size()) {
        d.error = "empty diagram";
        return d;
    }

    std::string first = Trim(lines[firstLine]);
    std::string firstLower = ToLower(first);

    // Detect diagram type
    if (StartsWith(firstLower, "flowchart ") || StartsWith(firstLower, "flowchart\t") ||
        firstLower == "flowchart") {
        d.type = DiagramType::Flowchart;
    } else if (StartsWith(firstLower, "graph ") || StartsWith(firstLower, "graph\t") ||
               firstLower == "graph") {
        d.type = DiagramType::Flowchart;
    } else if (StartsWith(firstLower, "sequencediagram")) {
        d.type = DiagramType::Sequence;
    } else {
        d.type = DiagramType::Unknown;
        d.error = "unsupported diagram type: " + first;
        return d;
    }

    // Parse direction for flowchart
    if (d.type == DiagramType::Flowchart) {
        // 'flowchart TD' or 'graph LR' etc.
        // Direction is the second word
        std::stringstream ss(first);
        std::string word1, word2;
        ss >> word1 >> word2;
        word2 = ToLower(word2);
        if (word2 == "td" || word2 == "tb" || word2.empty()) {
            d.dir = Direction::TD;
        } else if (word2 == "lr") {
            d.dir = Direction::LR;
        } else if (word2 == "rl") {
            d.dir = Direction::RL;
        } else if (word2 == "bt") {
            d.dir = Direction::BT;
        } else {
            d.dir = Direction::TD;
        }
    }

    std::unordered_map<std::string, int> idMap;

    // Parse content lines (after the first line)
    for (size_t i = firstLine + 1; i < lines.size(); i++) {
        std::string line = Trim(lines[i]);
        if (line.empty()) continue;
        if (StartsWith(line, "%%")) continue;

        if (d.type == DiagramType::Flowchart) {
            ParseFlowchartLine(d, idMap, line);
        } else if (d.type == DiagramType::Sequence) {
            ParseSeqLine(d, idMap, line);
        }
    }

    return d;
}

}  // namespace mermaid
