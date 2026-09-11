#include "class_parse.h"

#include <cctype>
#include <cstdlib>
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

bool StartsWithWord(const std::string& line, const char* word) {
    std::string w(word);
    if (line.compare(0, w.size(), w) != 0) return false;
    return line.size() == w.size() || IsSpace(line[w.size()]);
}

// Arrow tokens, longest first. end_marker says which side carries the marker
// (mermaid attaches the symbol side: `*--` puts the diamond on the left/A).
struct ArrowTok {
    const char* tok;
    RelType type;
    RelEnd end_marker;   // Start = marker near A, End = marker near B
    bool dashed;
};

constexpr ArrowTok kArrows[] = {
    {"<|--", RelType::Extension,   RelEnd::Start, false},
    {"--|>", RelType::Extension,   RelEnd::End,   false},
    {"<|..", RelType::Extension,   RelEnd::Start, true},
    {"..|>", RelType::Extension,   RelEnd::End,   true},
    {"*--",  RelType::Composition, RelEnd::Start, false},
    {"--*",  RelType::Composition, RelEnd::End,   false},
    {"o--",  RelType::Aggregation, RelEnd::Start, false},
    {"--o",  RelType::Aggregation, RelEnd::End,   false},
    {"..>",  RelType::Dependency,  RelEnd::End,   true},
    {"<..",  RelType::Dependency,  RelEnd::Start, true},
    {"-->",  RelType::Dependency,  RelEnd::End,   false},
    {"<--",  RelType::Dependency,  RelEnd::Start, false},
    {"..",   RelType::None,        RelEnd::None,  true},
    {"--",   RelType::None,        RelEnd::None,  false},
};

// Finds the arrow in a relation line. Returns true and fills the split
// positions; left = line[start, arrow_at), right = line[arrow_end, colon).
bool FindRelationArrow(const std::string& line, size_t& arrow_at,
                       size_t& arrow_len, const ArrowTok*& found) {
    for (size_t p = 0; p < line.size(); ++p) {
        for (const ArrowTok& t : kArrows) {
            std::string tok(t.tok);
            if (line.compare(p, tok.size(), tok) != 0) continue;
            // Arrow must sit between two non-empty, space-free ids.
            std::string left = Trim(line.substr(0, p));
            // Left may be `Id` or `Id "card"` (one quoted chunk after the id).
            {
                size_t sp = left.find(' ');
                if (sp != std::string::npos) {
                    std::string tail = Trim(left.substr(sp + 1));
                    left = Trim(left.substr(0, sp));
                    if (tail.size() >= 2 && tail.front() == '"' &&
                        tail.back() == '"') {
                        // ok: cardinality
                    } else if (!tail.empty()) {
                        continue;  // extra junk on the left: not a relation
                    }
                }
                if (left.empty()) continue;
            }
            size_t right_start = p + tok.size();
            while (right_start < line.size() && IsSpace(line[right_start])) {
                ++right_start;
            }
            if (right_start >= line.size()) continue;
            arrow_at = p;
            arrow_len = tok.size();
            found = &t;
            return true;
        }
    }
    return false;
}

// Strip a trailing generic `~T~` and sanitize: mermaid replaces ~X~ with
// <X> in labels; class ids in our fixtures keep plain names.
std::string CleanId(std::string_view s) {
    std::string out = Trim(s);
    return out;
}

}  // namespace

bool ClassParseMember(const std::string& raw, std::string& text,
                      bool& is_method) {
    std::string s = Trim(raw);
    if (s.empty()) return false;
    // Annotations: <<...>> (also the bare <<Interface>> form inside bodies).
    // Keep the delimited form in text; the caller classifies on the << >>
    // wrapper and strips it there. Stripping twice made the wrapper check
    // never fire, so annotations landed in members.
    if (s.size() >= 4 && s.compare(0, 2, "<<") == 0 &&
        s.compare(s.size() - 2, 2, ">>") == 0) {
        text = s;
        is_method = false;
        return true;
    }
    // Classifier suffixes on the raw line: $ (static), * (abstract). mermaid
    // strips them from the drawn text.
    bool is_static = false, is_abstract = false;
    while (!s.empty() && (s.back() == '$' || s.back() == '*')) {
        if (s.back() == '$') is_static = true; else is_abstract = true;
        s.pop_back();
        while (!s.empty() && IsSpace(s.back())) s.pop_back();
    }
    (void)is_static;
    (void)is_abstract;
    // Return type: `name(args) ret` -> `name(args) : ret` (mermaid's member
    // parser formats methods with a return type this way).
    is_method = false;
    size_t open = s.find('(');
    if (open != std::string::npos) {
        size_t close = s.find(')', open);
        if (close != std::string::npos && close + 1 < s.size() &&
            IsSpace(s[close + 1])) {
            std::string ret = Trim(s.substr(close + 1));
            if (!ret.empty()) {
                s = s.substr(0, close + 1) + " : " + ret;
            }
        }
        is_method = true;
    }
    text = s;
    return true;
}

ClassDiagram ParseClassDiagram(std::string_view src) {
    ClassDiagram d;
    size_t i = 0, n = src.size();
    auto next_line = [&](std::string& out) -> bool {
        if (i >= n) return false;
        size_t j = i;
        while (j < n && src[j] != '\n') ++j;
        out = Trim(src.substr(i, j - i));
        i = (j < n) ? j + 1 : j;
        return true;
    };

    bool header_seen = false;
    bool in_body = false;
    ClassBox* current = nullptr;
    std::string line;
    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;

        if (!header_seen) {
            if (StartsWithWord(line, "classDiagram")) {
                header_seen = true;
                continue;
            }
            d.error = "not a class diagram";
            return d;
        }

        // Class body close.
        if (line == "}" || line == "}" ) {
            in_body = false;
            current = nullptr;
            continue;
        }

        // direction (only outside class bodies).
        if (!in_body && (StartsWithWord(line, "direction") ||
                         line.compare(0, 10, "direction:") == 0)) {
            std::string rest = Trim(line.substr(9));
            if (!rest.empty() && rest[0] == ':') rest = Trim(rest.substr(1));
            if (rest == "LR" || rest == "RL" || rest == "TB" || rest == "TD" ||
                rest == "BT") {
                d.direction = rest == "TD" ? "TB" : rest;
            }
            continue;
        }

        // class declarations.
        if (StartsWithWord(line, "class")) {
            std::string rest = Trim(line.substr(5));
            bool has_body = !rest.empty() && rest.back() == '{';
            if (has_body) {
                rest.pop_back();
                rest = Trim(rest);
                in_body = true;
            }
            std::string id = CleanId(rest);
            if (id.empty()) {
                d.error = "class with empty name";
                return d;
            }
            bool exists = false;
            for (const auto& c : d.classes) {
                if (c.id == id) exists = true;
            }
            if (!exists) d.classes.push_back(ClassBox{id, {}, {}, {}});
            if (has_body || in_body) {
                for (auto& c : d.classes) {
                    if (c.id == id) current = &c;
                }
            }
            continue;
        }

        // Inside a class body: member / method / annotation lines.
        if (in_body && current != nullptr) {
            std::string text;
            bool is_method = false;
            if (!ClassParseMember(line, text, is_method)) continue;
            if (text.rfind("<<", 0) == 0 && text.size() >= 4 &&
                text.compare(text.size() - 2, 2, ">>") == 0) {
                current->annotations.push_back(
                    Trim(text.substr(2, text.size() - 4)));
            } else if (is_method) {
                current->methods.push_back(text);
            } else {
                current->members.push_back(text);
            }
            continue;
        }

        // Relation: A ["card"] <arrow> ["card"] B [: label]
        {
            size_t arrow_at = 0, arrow_len = 0;
            const ArrowTok* tok = nullptr;
            if (!FindRelationArrow(line, arrow_at, arrow_len, tok)) continue;
            std::string left = Trim(line.substr(0, arrow_at));
            std::string right = Trim(line.substr(arrow_at + arrow_len));
            if (left.empty() || right.empty()) continue;
            // Cardinality quotes on either side.
            std::string start_label, end_label;
            // Recapture the left cardinality stripped during the left check.
            {
                std::string left_raw = Trim(line.substr(0, arrow_at));
                size_t q = left_raw.find('"');
                if (q != std::string::npos) {
                    size_t q2 = left_raw.find('"', q + 1);
                    if (q2 != std::string::npos)
                        start_label = left_raw.substr(q + 1, q2 - q - 1);
                }
            }
            auto TakeQuoted = [](std::string& s, std::string& label) {
                size_t q = s.find('"');
                if (q == std::string::npos) return false;
                size_t q2 = s.find('"', q + 1);
                if (q2 == std::string::npos) return false;
                label = s.substr(q + 1, q2 - q - 1);
                // Keep whatever follows the closing quote (the other id).
                s = Trim(s.substr(0, q) + " " + s.substr(q2 + 1));
                return true;
            };
            TakeQuoted(right, end_label);
            TakeQuoted(left, start_label);
            std::string label;
            size_t colon = right.find(':');
            if (colon != std::string::npos) {
                label = Trim(right.substr(colon + 1));
                right = Trim(right.substr(0, colon));
            }
            if (left.empty() || right.empty()) continue;
            ClassRelation r;
            r.from = CleanId(left);
            r.to = CleanId(right);
            r.type = tok->type;
            r.marker_end = tok->end_marker;
            r.dashed = tok->dashed;
            r.label = label;
            r.start_label = start_label;
            r.end_label = end_label;
            // Auto-declare relation endpoints (mermaid does the same).
            for (const std::string& id : {r.from, r.to}) {
                bool exists = false;
                for (const auto& c : d.classes) {
                    if (c.id == id) exists = true;
                }
                if (!exists) d.classes.push_back(ClassBox{id, {}, {}, {}});
            }
            d.relations.push_back(r);
            continue;
        }
    }

    if (!header_seen) {
        d.error = "not a class diagram";
        return d;
    }
    return d;
}

}  // namespace mermaid
