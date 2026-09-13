// State diagram parser: flat subset of mermaid stateDiagram-v2.
// Mirrors the class parser structure (Trim, FindRelationArrow-style scan).
#include "state_parse.h"

#include <cctype>

namespace mermaid {

namespace {

std::string Trim(std::string s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Find " --> " / "-->" arrow; returns position of '-' or npos.
size_t FindArrow(const std::string& s, size_t& len) {
    for (size_t i = 0; i + 3 < s.size(); ++i) {
        if (s[i] == '-' && s[i + 1] == '-' && s[i + 2] == '>') {
            len = 3;
            return i;
        }
    }
    return std::string::npos;
}

bool IsStartMarker(const std::string& s) { return s == "[*]"; }

// The start marker aliases both implicit root_start and every [*] on the
// left; the end marker aliases root_end on the right. Keep them as special
// ids so the layout can synthesize the geometry.
std::string CleanId(std::string s) {
    s = Trim(std::move(s));
    if (s == "[*]") return s;
    // Strip stray quotes (should not occur in this subset).
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

StateDiagram ParseStateDiagram(std::string_view src) {
    StateDiagram d;
    bool header_seen = false;

    auto declare = [&](const std::string& id) {
        for (const auto& n : d.nodes)
            if (n.id == id) return;
        StateNode n;
        n.id = id;
        n.text = id;
        d.nodes.push_back(n);
    };

    std::string line;
    for (size_t i = 0; i <= src.size(); ++i) {
        char ch = (i < src.size()) ? src[i] : '\n';
        if (ch == '\n' || ch == '\r') {
            if (!line.empty()) {
                std::string t = Trim(line);
                if (!t.empty() && t.rfind("%%", 0) != 0) {
                    if (!header_seen) {
                        if (t.rfind("stateDiagram", 0) == 0 ||
                            t.rfind("stateDiagram-v2", 0) == 0) {
                            header_seen = true;
                        } else {
                            d.error = "not a state diagram";
                            return d;
                        }
                    } else if (t.rfind("direction", 0) == 0) {
                        std::string dir = Trim(t.substr(9));
                        if (dir == "TB" || dir == "LR" || dir == "RL" || dir == "BT")
                            d.direction = dir;
                    } else if (t.rfind("state ", 0) == 0) {
                        std::string rest = Trim(t.substr(6));
                        // state X { ... } -> composite (declared, layout TODO)
                        size_t brace = rest.find('{');
                        if (brace != std::string::npos) {
                            std::string id = CleanId(rest.substr(0, brace));
                            if (id.empty() || id == "[*]") { d.error = "bad composite state"; return d; }
                            declare(id);
                            d.nodes.back().kind = StateKind::Composite;
                            d.nodes.back().has_description = true;
                            // Parse the contained transitions until the
                            // matching close brace.
                            size_t depth = 1;
                            size_t j = line.find('{');
                            std::string inner = line;
                            // multi-line composite: scan forward
                            while (depth > 0) {
                                size_t next = inner.find('}', j + 1);
                                size_t open = inner.find('{', j + 1);
                                bool has_open = open != std::string::npos;
                                bool has_close = next != std::string::npos;
                                if (has_open && (!has_close || open < next)) { ++depth; j = open; }
                                else if (has_close) { --depth; j = next; }
                                else {
                                    // continue on next line
                                    ++i;
                                    if (i >= src.size()) { d.error = "unterminated composite"; return d; }
                                    inner += "\n";
                                    while (i < src.size() && src[i] != '\n' && src[i] != '\r') {
                                        inner += src[i]; ++i;
                                    }
                                    j = inner.size() - 1;
                                }
                            }
                        } else {
                            // state X ;  or  state "desc" as X
                            std::string id = CleanId(rest);
                            if (id.empty() || id == "[*]") { d.error = "bad state id"; return d; }
                            declare(id);
                        }
                    } else {
                        // transition: A --> B : label
                        size_t alen = 0;
                        size_t at = FindArrow(t, alen);
                        if (at == std::string::npos) continue;
                        std::string left = Trim(t.substr(0, at));
                        std::string right = Trim(t.substr(at + alen));
                        std::string label;
                        size_t colon = right.find(':');
                        if (colon != std::string::npos) {
                            label = Trim(right.substr(colon + 1));
                            right = Trim(right.substr(0, colon));
                        }
                        if (left.empty() || right.empty()) continue;
                        std::string from = CleanId(left);
                        std::string to = CleanId(right);
                        if (from.empty() || to.empty()) continue;
                        if (IsStartMarker(from)) {
                            // ensure implicit start node exists once
                            static const std::string kStart = "root_start";
                            auto it = d.nodes.begin();
                            for (; it != d.nodes.end(); ++it)
                                if (it->id == kStart) break;
                            if (it == d.nodes.end()) {
                                StateNode n; n.id = kStart; n.kind = StateKind::Start;
                                d.nodes.insert(d.nodes.begin(), n);
                            }
                            from = kStart;
                        }
                        if (IsStartMarker(to) || to == "root_end" || to == "[*]") {
                            static const std::string kEnd = "root_end";
                            auto it = d.nodes.begin();
                            for (; it != d.nodes.end(); ++it)
                                if (it->id == kEnd) break;
                            if (it == d.nodes.end()) {
                                StateNode n; n.id = kEnd; n.kind = StateKind::End;
                                d.nodes.push_back(n);
                            }
                            to = kEnd;
                        } else if (IsStartMarker(from)) {
                            // handled above
                        }
                        declare(from);
                        declare(to);
                        d.transitions.push_back(StateTransition{from, to, label});
                    }
                }
                line.clear();
            }
            continue;
        }
        line += ch;
    }

    if (!header_seen) {
        d.error = "not a state diagram";
        return d;
    }
    return d;
}

}  // namespace mermaid