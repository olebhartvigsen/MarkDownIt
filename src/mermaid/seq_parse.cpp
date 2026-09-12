#include "seq_parse.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace mermaid {

bool MsgIsMessage(MsgType t) {
    return t == MsgType::Solid || t == MsgType::Dotted ||
           t == MsgType::SolidOpen || t == MsgType::DottedOpen ||
           t == MsgType::SolidCross || t == MsgType::DottedCross ||
           t == MsgType::SolidPoint || t == MsgType::DottedPoint ||
           t == MsgType::SolidAsync || t == MsgType::DottedAsync;
}
bool MsgIsDotted(MsgType t) {
    return t == MsgType::Dotted || t == MsgType::DottedOpen ||
           t == MsgType::DottedCross || t == MsgType::DottedPoint ||
           t == MsgType::DottedAsync;
}
bool MsgOpenHead(MsgType t) {
    return t == MsgType::SolidOpen || t == MsgType::DottedOpen ||
           t == MsgType::SolidAsync || t == MsgType::DottedAsync;
}
bool MsgCrossHead(MsgType t) {
    return t == MsgType::SolidCross || t == MsgType::DottedCross;
}
bool MsgPointHead(MsgType t) {
    return t == MsgType::SolidPoint || t == MsgType::DottedPoint;
}

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

bool IEquals(const std::string& a, const char* b) {
    std::string s(b);
    if (a.size() != s.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(s[i])))
            return false;
    }
    return true;
}

// Arrow grammar with activation suffixes folded in (mermaid's
// sequenceDiagram parser ARROW token list):
//   SOLID: '->>' DOTTED '-->>' SOLID_CROSS '->x' DOTTED_CROSS '--x'
//   SOLID_POINT '->o' DOTTED_POINT '--o' ASYNC '-)' ASYNC_DOTTED '--)'
//   OPEN '->' OPEN_DOTTED '-->'
// Activation suffix forms: `->>+` `->>+ +` `-->>-` `-x+` etc. The suffix
// applies +/-1 to the SOURCE (leading, before actor) and +1 per '+' on the
// TARGET (trailing). Canonical cases covered: leading '++' (both), leading
// '+', trailing '+', trailing '-'.
struct ArrowTok {
    const char* core;    // without suffix chars
    MsgType type;
    bool dotted;
};
// longest first so prefix shadows never win
constexpr ArrowTok kArrows[] = {
    {"-->>", MsgType::Dotted, true},
    {"->>",  MsgType::Solid, false},
    {"->x",  MsgType::SolidCross, false},
    {"--x",  MsgType::DottedCross, true},
    {"->o",  MsgType::SolidPoint, false},
    {"--o",  MsgType::DottedPoint, true},
    {"->+",  MsgType::Solid, false},      // suffix-dotted core '->'
    {"->",   MsgType::SolidOpen, false},
    {"-->",  MsgType::DottedOpen, true},
    {"-)",   MsgType::SolidAsync, false},
    {"--)",  MsgType::DottedAsync, true},
};

// Matches an arrow at exactly position `p` in s. Suffix chars following the
// core (+/-) are consumed and folded into `plus`/`minus` counts. Returns the
// total consumed length (core + suffixes), 0 if no match.
size_t MatchArrowAt(const std::string& s, size_t p, MsgType& type,
                    int& plus_after, int& minus_after) {
    // No actor id starts with '-' in our grammar subset; arrows are '-'-run
    // forms. Try longest token chains first: the arrow family shares prefixes
    // so ordering matters.
    static const char* kCores[] = {
        "-->>", "->>", "->x", "--x", "->o", "--o", "-->", "->", "-)", "--)",
    };
    static const MsgType kTType[] = {
        MsgType::Dotted, MsgType::Solid, MsgType::SolidCross,
        MsgType::DottedCross, MsgType::SolidPoint, MsgType::DottedPoint,
        MsgType::DottedOpen, MsgType::SolidOpen,
        MsgType::SolidAsync, MsgType::DottedAsync,
    };
    for (size_t i = 0; i < 10; ++i) {
        size_t len = std::string(kCores[i]).size();
        if (s.compare(p, len, kCores[i]) != 0) continue;
        type = kTType[i];
        size_t end = p + len;
        plus_after = 0;
        minus_after = 0;
        while (end < s.size() && s[end] == '+') { ++plus_after; ++end; }
        while (end < s.size() && s[end] == '-') { ++minus_after; ++end; }
        return end - p;
    }
    return 0;
}

// Finds the arrow in `line` = "<from><arrow><to>: ..." scanning positions.
// Returns arrow END index; from = [0, start), to = [end, colon).
size_t FindArrowSplit(const std::string& line, MsgType& type,
                      int& plus_after, int& minus_after,
                      size_t& arrow_start) {
    for (size_t p = 0; p < line.size(); ++p) {
        size_t consumed = MatchArrowAt(line, p, type, plus_after, minus_after);
        if (consumed == 0) continue;
        std::string left = line.substr(0, p);
        if (left.empty() || left.find(' ') != std::string::npos ||
            left.find('\t') != std::string::npos)
            continue;
        size_t colon = line.find(':', p + consumed);
        if (colon == std::string::npos) continue;
        std::string right = line.substr(p + consumed, colon - (p + consumed));
        if (right.empty() || right.find(' ') != std::string::npos ||
            right.find('\t') != std::string::npos)
            continue;
        arrow_start = p;
        return p + consumed;
    }
    return 0;
}

}  // namespace

namespace {

// Finds or appends a participant. Undeclared actors discovered in messages
// are appended in first-seen order (mermaid's behavior).
size_t InternParticipant(SequenceDiagram& seq, const std::string& id,
                         ActorShape shape = ActorShape::Participant) {
    for (size_t i = 0; i < seq.participants.size(); ++i) {
        if (seq.participants[i].id == id) return i;
    }
    SeqParticipant p;
    p.id = id;
    p.display = id;
    p.shape = shape;
    p.declared = false;
    seq.participants.push_back(p);
    return seq.participants.size() - 1;
}

}  // namespace

SequenceDiagram ParseSequence(std::string_view src) {
    SequenceDiagram seq;
    using Item = SequenceDiagram::Item;
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
    std::vector<int> open_loops;  // loop_index of currently open loop structs
    std::string line;
    while (next_line(line)) {
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;

        if (!header_seen) {
            if (StartsWithWord(line, "sequenceDiagram")) {
                header_seen = true;
                // optional modifiers: `sequenceDiagram autonumber`
                std::string rest = Trim(line.substr(std::string("sequenceDiagram").size()));
                if (rest.find("autonumber") != std::string::npos) seq.autonumber = true;
                continue;
            }
            seq.error = "not a sequence diagram";
            return seq;
        }

        // ---- title ----
        if (StartsWithWord(line, "title") ||
            (line.compare(0, 6, "title:") == 0)) {
            // `title Some text` or `title: Some text`
            if (line.compare(0, 5, "title") == 0) {
                std::string rest = line.substr(5);
                if (!rest.empty() && rest[0] == ':') rest = rest.substr(1);
                seq.title = Trim(rest);
            }
            continue;
        }
        if (line.compare(0, 10, "autonumber") == 0) {
            seq.autonumber = true;
            continue;
        }
        // ---- participant / actor ----
        if (StartsWithWord(line, "participant") || StartsWithWord(line, "actor")) {
            bool stick = line[0] == 'a';
            std::string rest = Trim(line.substr(stick ? std::string("actor").size()
                                                      : std::string("participant").size()));
            if (rest.empty()) { seq.error = "empty participant"; return seq; }
            // participant <id> as <display>   |   participant <id>, display
            std::string id = rest, display;
            size_t as_pos = rest.find(" as ");
            if (as_pos != std::string::npos) {
                id = Trim(rest.substr(0, as_pos));
                display = Trim(rest.substr(as_pos + 4));
            }
            size_t idx = InternParticipant(seq, id, stick ? ActorShape::ActorStickman
                                                          : ActorShape::Participant);
            if (!display.empty()) seq.participants[idx].display = display;
            seq.participants[idx].declared = true;
            continue;
        }

        // ---- Note ----
        // mermaid keywords are case-insensitive: both `Note over A: ...` and
        // `note over A: ...` must parse (the MSK integration doc uses lowercase).
        if (StartsWithWord(line, "Note") || StartsWithWord(line, "note")) {
            std::string rest = Trim(line.substr(4));
            NotePlacement place = NotePlacement::Over;
            size_t id_start = 0;
            if (rest.compare(0, 8, "right of") == 0) {
                place = NotePlacement::RightOf;
                id_start = 8;
            } else if (rest.compare(0, 7, "left of") == 0) {
                place = NotePlacement::LeftOf;
                id_start = 7;
            } else if (rest.compare(0, 4, "over") == 0) {
                id_start = 4;
            } else {
                seq.error = "note: missing over/left of/right of";
                return seq;
            }
            while (id_start < rest.size() && IsSpace(rest[id_start])) ++id_start;
            std::string tail = rest.substr(id_start);
            size_t colon = tail.find(':');
            if (colon == std::string::npos) {
                seq.error = "note: missing ':'";
                return seq;
            }
            std::string ids = Trim(tail.substr(0, colon));
            std::string text = Trim(tail.substr(colon + 1));
            SeqMessage m;
            m.type = MsgType::Note;
            m.placement = place;
            size_t comma = ids.find(',');
            if (comma != std::string::npos) {
                m.from = Trim(ids.substr(0, comma));
                m.to = Trim(ids.substr(comma + 1));
                InternParticipant(seq, m.from);
                InternParticipant(seq, m.to);
            } else {
                m.from = ids;
                m.to = ids;
                InternParticipant(seq, ids);
            }
            m.text = text;
            seq.messages.push_back(m);
            Item it;
            it.type = MsgType::Note;
            it.msg_index = static_cast<int>(seq.messages.size()) - 1;
            seq.items.push_back(it);
            continue;
        }

        // ---- activate / deactivate ----
        if (StartsWithWord(line, "activate")) {
            std::string id = Trim(line.substr(8));
            size_t idx = InternParticipant(seq, id);
            Item it;
            it.type = MsgType::ActiveStart;
            it.actor_index = static_cast<int>(idx);
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "deactivate")) {
            std::string id = Trim(line.substr(10));
            size_t idx = InternParticipant(seq, id);
            Item it;
            it.type = MsgType::ActiveEnd;
            it.actor_index = static_cast<int>(idx);
            seq.items.push_back(it);
            continue;
        }

        // ---- loop / alt / opt / par / critical / break / rect ----
        if (StartsWithWord(line, "loop")) {
            seq.loops.push_back({"loop", Trim(line.substr(4)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::LoopStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "alt")) {
            seq.loops.push_back({"alt", Trim(line.substr(3)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::AltStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "opt")) {
            seq.loops.push_back({"opt", Trim(line.substr(3)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::OptStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "par")) {
            seq.loops.push_back({"par", Trim(line.substr(3)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::ParStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "critical")) {
            seq.loops.push_back({"critical", Trim(line.substr(8)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::CriticalStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "break")) {
            seq.loops.push_back({"break", Trim(line.substr(5)), ""});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::BreakStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "rect")) {
            std::string rest = Trim(line.substr(4));
            std::string fill;
            size_t op = rest.find('(');
            size_t cp = rest.find(')');
            if (op != std::string::npos && cp != std::string::npos && cp > op) {
                fill = rest.substr(op, cp - op + 1);
                if (fill.compare(0, 4, "rgb(") != 0 && fill.compare(0, 5, "rgba(") != 0) {
                    fill = rest;
                }
            } else if (!rest.empty()) {
                fill = rest;
            }
            seq.loops.push_back({"rect", fill, fill});
            open_loops.push_back(static_cast<int>(seq.loops.size()) - 1);
            Item it; it.type = MsgType::RectStart;
            it.loop_index = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (IEquals(line, "end")) {
            int idx = open_loops.empty() ? -1 : open_loops.back();
            if (idx >= 0) open_loops.pop_back();
            Item it;
            it.type = MsgType::LoopEnd;
            it.loop_index = idx;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "else")) {
            std::string label = Trim(line.substr(4));
            seq.loops.push_back({"else", label, ""});
            Item it; it.type = MsgType::AltElse;
            it.loop_index = open_loops.empty() ? -1 : open_loops.back();
            it.loop_id = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "and")) {
            std::string label = Trim(line.substr(3));
            seq.loops.push_back({"and", label, ""});
            Item it; it.type = MsgType::ParAnd;
            it.loop_index = open_loops.empty() ? -1 : open_loops.back();
            it.loop_id = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }
        if (StartsWithWord(line, "option")) {
            std::string label = Trim(line.substr(6));
            seq.loops.push_back({"option", label, ""});
            Item it; it.type = MsgType::CriticalOption;
            it.loop_index = open_loops.empty() ? -1 : open_loops.back();
            it.loop_id = static_cast<int>(seq.loops.size()) - 1;
            seq.items.push_back(it);
            continue;
        }

        // ---- messages ----
        {
            MsgType type;
            int plus_after, minus_after;
            size_t arrow_start = 0;
            size_t split = FindArrowSplit(line, type, plus_after,
                                          minus_after, arrow_start);
            if (split == 0) continue;
            std::string from = Trim(line.substr(0, arrow_start));
            if (!from.empty() && from.front() == '+') {
                from.erase(0, 1);   // leading + = source activation
                ++plus_after;
            }
            std::string rest = line.substr(split);
            size_t colon = rest.find(':');
            std::string to_part = Trim(rest.substr(0, colon));
            std::string text = colon == std::string::npos ? "" : Trim(rest.substr(colon + 1));
            // Mermaid's ACTOR token never contains '+' or '-'; stray suffix
            // characters that did not sit flush against the arrow are not
            // activation syntax (mermaid parse-errors on them; we strip and
            // render the message, a documented lenient divergence).
            while (!from.empty() && (from.back() == '+' || from.back() == '-'))
                from.pop_back();
            while (!to_part.empty() && (to_part.back() == '+' || to_part.back() == '-'))
                to_part.pop_back();
            if (from.empty() || to_part.empty()) {
                seq.error = "message with empty actor";
                return seq;
            }
            InternParticipant(seq, from);
            InternParticipant(seq, to_part);
            // Grammar (mermaid jison cases 65-67): a trailing `+` right after
            // the arrow expands to message + ACTIVE_START on the target; a
            // trailing `-` expands to message + ACTIVE_END on the source.
            // mermaid errors on mixed/double suffixes at the arrow; accept at
            // most one of each (lenient).
            bool opens_target = plus_after > 0 && minus_after == 0;
            bool closes_source = minus_after > 0 && plus_after == 0;
            SeqMessage m;
            m.from = from;
            m.to = to_part;
            m.type = type;
            m.text = text;
            m.activation_delta = plus_after - minus_after;
            seq.messages.push_back(m);
            Item it;
            it.type = type;
            it.msg_index = static_cast<int>(seq.messages.size()) - 1;
            seq.items.push_back(it);
            if (opens_target) {
                size_t idx = InternParticipant(seq, to_part);
                Item st;
                st.type = MsgType::ActiveStart;
                st.actor_index = static_cast<int>(idx);
                seq.items.push_back(st);
            }
            if (closes_source) {
                size_t idx = InternParticipant(seq, from);
                Item en;
                en.type = MsgType::ActiveEnd;
                en.actor_index = static_cast<int>(idx);
                seq.items.push_back(en);
            }
            continue;
        }
    }

    if (!header_seen) {
        seq.error = seq.error.empty() ? "not a sequence diagram" : seq.error;
        return seq;
    }
    return seq;
}

}  // namespace mermaid
