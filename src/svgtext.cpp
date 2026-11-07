#include "svgtext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace svg {

// --- Entity decoder ---

static std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos" || ent == "#39") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            int code = 0;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                code = static_cast<int>(std::strtol(ent.c_str() + 2, nullptr, 16));
            else
                code = static_cast<int>(std::strtol(ent.c_str() + 1, nullptr, 10));
            if (code > 0 && code < 0x110000) {
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
        } else {
            out.push_back('&');
        }
        i = semi; // skip to semicolon
    }
    return out;
}

// --- Attribute parser ---

static std::string GetAttr(const std::string& tag, const std::string& name) {
    // Attribute lookup with a word-boundary guard: a plain find
    // matches suffixes too, e.g. y="0" inside dy="0", or width="1"
    // inside stroke-width="1". Require a non-name char (the
    // whitespace separator, or the tag start) before the name.
    const char qs[2] = {34, 39};
    std::string pat;
    size_t a = std::string::npos;
    for (int qi = 0; qi < 2 && a == std::string::npos; ++qi) {
        char q2 = qs[qi];
        pat = name + "=" + std::string(1, q2);
        size_t p2 = 0;
        while ((p2 = tag.find(pat, p2)) != std::string::npos) {
            if (p2 == 0 ||
                !(std::isalnum(static_cast<unsigned char>(tag[p2 - 1])) ||
                  tag[p2 - 1] == 45 || tag[p2 - 1] == 95 ||
                  tag[p2 - 1] == 58)) {
                a = p2;
                break;
            }
            p2 += pat.size();
        }
    }
    if (a == std::string::npos) return {};
    a += pat.size();
    char q = tag[a - 1];
    size_t end = tag.find(q, a);
    if (end == std::string::npos) return {};
    return tag.substr(a, end - a);
}

static float ToFloat(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    try {
        const float v = std::stof(s);
        // std::stof stops at the first non-numeric character, so "12pt" came
        // back as a bare 12. These are the CSS/SVG absolute conversions at the
        // usual 96dpi; unitless values stay as they are.
        size_t p = 0;
        while (p < s.size() && (std::isdigit(static_cast<unsigned char>(s[p])) ||
                                s[p] == '.' || s[p] == '-' || s[p] == '+')) ++p;
        std::string unit;
        for (size_t k = p; k < s.size(); ++k) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
            if (std::isalpha(static_cast<unsigned char>(c))) unit.push_back(c);
            else if (c != '%' ) break;
            else unit.push_back('%');
        }
        if (unit == "pt") return v * 1.25f;
        if (unit == "pc") return v * 16.0f;
        if (unit == "in") return v * 96.0f;
        if (unit == "cm") return v * 96.0f / 2.54f;
        if (unit == "mm") return v * 96.0f / 25.4f;
        if (unit == "em" || unit == "rem") return v * 16.0f;  // ~1em default
        return v;
    } catch (...) {
        return def;
    }
}

// A parsed <style> rule set: selector suffix ("lab" for ".lab") to the
// declaration body. Only class selectors are handled, which is what Inkscape,
// Graphviz and hand-written files actually use.
//
// `byName` indexes the same entries by class name so a lookup does not scan
// every rule. Scanning made styling quadratic in rules times styled elements:
// a sheet of 8000 rules cost 6 ms, 16000 rules 39 ms, 32000 rules 191 ms and
// 64000 rules 824 ms, growing 4.3x per doubling. Only the FIRST rule for a
// name is indexed, so the cascade still resolves a repeated selector to the
// same declaration it did before.
struct StyleSheet {
    std::vector<std::pair<std::string, std::string>> rules;
    std::unordered_map<std::string, size_t> byName;

    void Add(const std::string& name, const std::string& body) {
        // First declaration wins, matching the previous scan-and-break.
        auto it = byName.find(name);
        if (it != byName.end()) return;
        byName.emplace(name, rules.size());
        rules.emplace_back(name, body);
    }

    const std::string* Find(const std::string& name) const {
        auto it = byName.find(name);
        if (it == byName.end()) return nullptr;
        return &rules[it->second].second;
    }
};

// Trim ASCII whitespace from both ends of a string.
//
// This used to erase one character at a time from the front, and every
// std::string::erase shifts the characters after it, so trimming N leading
// spaces cost O(N^2). A stylesheet with 20000 spaces before a declaration took
// 24 ms and grew 4.2x per doubling, so 160000 spaces took 1.5 s for a document
// of 160 KB. Locating the bounds with indices and substr-ing once is O(N).
static std::string TrimAscii(const std::string& in) {
    size_t b = 0;
    size_t e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return in.substr(b, e - b);
}

// Pull ".name { prop: value; ... }" pairs out of a <style> body. Element and
// id selectors are ignored rather than misapplied.
static StyleSheet ParseStyleSheet(const std::string& css) {
    StyleSheet sheet;
    size_t i = 0;
    while (i < css.size()) {
        const size_t brace = css.find('{', i);
        if (brace == std::string::npos) break;
        // Trim and keep only the last simple selector.
        const std::string selector = TrimAscii(css.substr(i, brace - i));
        const size_t close = css.find('}', brace);
        if (close == std::string::npos) break;
        std::string body = css.substr(brace + 1, close - brace - 1);
        // Selector must be a plain ".class", optionally with other selectors we
        // do not understand; only apply when it is exactly one class.
        if (selector.size() >= 2 && selector[0] == '.' &&
            selector.find_first_of(" ,>+~:") == std::string::npos &&
            selector.find('.', 1) == std::string::npos) {
            sheet.Add(selector.substr(1), body);
        }
        i = close + 1;
    }
    return sheet;
}

// The declaration body for a class attribute's first matching class, if any.
static std::string ClassRule(const StyleSheet& sheet, const std::string& tag) {
    const std::string cls = GetAttr(tag, "class");
    if (cls.empty() || sheet.rules.empty()) return {};
    std::string pick;
    size_t start = 0;
    while (start <= cls.size()) {
        size_t sp = cls.find_first_of(" \t\n\r", start);
        const std::string name = cls.substr(start, sp == std::string::npos ? std::string::npos
                                                                          : sp - start);
        if (!name.empty()) {
            // Indexed lookup instead of a scan over every rule.
            if (const std::string* body = sheet.Find(name)) {
                if (!body->empty()) return *body;
            }
        }
        if (sp == std::string::npos) break;
        start = sp + 1;
    }
    return {};
}

// Declaration lookup inside a rule body such as "fill:#f00;font-size:9px".
static std::string RuleValue(const std::string& body, const std::string& prop) {
    size_t p = 0;
    while (p < body.size()) {
        const size_t colon = body.find(':', p);
        if (colon == std::string::npos) return {};
        const std::string key = TrimAscii(body.substr(p, colon - p));
        const size_t semi = body.find(';', colon);
        const std::string val = TrimAscii(body.substr(colon + 1,
            semi == std::string::npos ? std::string::npos : semi - colon - 1));
        if (key == prop) return val;
        if (semi == std::string::npos) return {};
        p = semi + 1;
    }
    return {};
}

static std::string GetStyleValue(const std::string& tag, const std::string& prop) {
    std::string style = GetAttr(tag, "style");
    if (style.empty()) return {};
    std::string lp = prop + ":";
    size_t p = style.find(lp);
    if (p == std::string::npos) return {};
    p += lp.size();
    while (p < style.size() && (style[p] == ' ' || style[p] == '\t')) ++p;
    size_t end = style.find(';', p);
    if (end == std::string::npos) end = style.size();
    return style.substr(p, end - p);
}

// Get an attribute, then the inline style attribute, then a matching class
// rule from any <style> block. Presentation attributes and inline styles win
// over the stylesheet, which is the CSS cascade order.
static std::string GetAttrOrStyle(const std::string& tag, const std::string& name,
                                  const StyleSheet* sheet = nullptr) {
    std::string v = GetAttr(tag, name);
    if (!v.empty()) return v;
    v = GetStyleValue(tag, name);
    if (!v.empty()) return v;
    if (sheet && !sheet->rules.empty()) {
        const std::string body = ClassRule(*sheet, tag);
        if (!body.empty()) {
            v = RuleValue(body, name);
            if (!v.empty()) return v;
        }
    }
    return {};
}

// Check if the tag name matches (case-insensitive).
// True for "<rect .../>" as NextTag returns it: the tag text keeps its
// trailing '/>' rather than just the name.
static bool IsSelfClosed(const std::string& tag) {
    return tag.size() >= 2 && tag[tag.size() - 1] == '>' && tag[tag.size() - 2] == '/';
}

static bool TagIs(const std::string& tag, const char* name) {
    size_t i = 1; // skip '<'
    if (i < tag.size() && tag[i] == '/') ++i;
    size_t j = 0;
    if (name[0] == '/') ++j;  // closing-tag name: skip its slash too
    if (j == 1 && !(tag.size() > 1 && tag[1] == '/')) {
        return false;  // open/close mismatch: name "/x" vs tag "<x"
    }
    while (name[j] && i < tag.size()) {
        char a = tag[i];
        char b = name[j];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        ++i; ++j;
    }
    return name[j] == 0 && (i >= tag.size() || tag[i] == ' ' || tag[i] == '\t'
           || tag[i] == '\n' || tag[i] == '\r' || tag[i] == '>' || tag[i] == '/');
}

// --- Tag extraction helper ---

struct XmlPos {
    const std::string& xml;
    size_t pos;

    XmlPos(const std::string& x) : xml(x), pos(0) {}

    // Find next tag starting with '<'. Returns the full tag including < and >.
    // For open tags like <text ...>, returns the tag.
    // For closing tags like </text>, returns that.
    // For self-closing <rect .../>, returns that.
    // For <!-- comments -->, skips them.
    // For <?xml ...?>, skips them.
    // Returns empty string at end.
    std::string NextTag() {
        while (pos < xml.size()) {
            size_t lt = xml.find('<', pos);
            if (lt == std::string::npos) return {};
            pos = lt;

            // Comment
            if (xml.compare(lt, 4, "<!--") == 0) {
                size_t end = xml.find("-->", lt + 4);
                if (end == std::string::npos) { pos = xml.size(); return {}; }
                pos = end + 3;
                continue;
            }
            // CDATA is literal character data and may itself contain '>',
            // so it must be consumed by its own terminator rather than by the
            // first '>'.
            if (xml.compare(lt, 9, "<![CDATA[") == 0) {
                const size_t end = xml.find("]]>", lt + 9);
                if (end == std::string::npos) { pos = xml.size(); return {}; }
                pos = end + 3;
                continue;
            }
            // Processing instruction or DOCTYPE
            if (lt + 1 < xml.size() && (xml[lt + 1] == '?' || xml[lt + 1] == '!')) {
                size_t end = xml.find('>', lt + 2);
                if (end == std::string::npos) { pos = xml.size(); return {}; }
                pos = end + 1;
                continue;
            }

            // Regular tag
            size_t gt = xml.find('>', lt + 1);
            if (gt == std::string::npos) { pos = xml.size(); return {}; }
            std::string tag = xml.substr(lt, gt - lt + 1);
            pos = gt + 1;
            return tag;
        }
        return {};
    }

    // True when the next thing is a <![CDATA[ block.
    bool AtCData() const {
        return pos + 9 <= xml.size() && xml.compare(pos, 9, "<![CDATA[") == 0;
    }

    // Consume a <![CDATA[ ... ]]> block and return its contents verbatim.
    std::string TakeCData() {
        if (!AtCData()) return {};
        const size_t end = xml.find("]]>", pos + 9);
        const std::string body = xml.substr(pos + 9,
            (end == std::string::npos ? xml.size() : end) - (pos + 9));
        pos = (end == std::string::npos) ? xml.size() : end + 3;
        return body;
    }

    // The next tag, without consuming it. Used to look ahead for a
    // <textPath> that is the first child of a <text> without committing to
    // the lookahead.
    std::string PeekTag() {
        const size_t save = pos;
        std::string t = NextTag();
        pos = save;
        return t;
    }

    // Extract text content between current pos and next '<'.
    // Advances pos to just before the '<'.
    std::string TextUntilTag() {
        size_t lt = xml.find('<', pos);
        if (lt == std::string::npos) lt = xml.size();
        std::string text = xml.substr(pos, lt - pos);
        pos = lt;
        return text;
    }
};

// --- ExtractTextRuns ---

std::vector<TextRun> ExtractTextRuns(const std::string& xml) {
    std::vector<TextRun> runs;
    XmlPos p(xml);

    // Column-style helpers: transform + style inheritance stack.
    struct Ctx {
        float m[6] = {1, 0, 0, 1, 0, 0};  // a b c d e f
        float fontSize = 16;
        std::string fontFamily;
        std::string anchor;
        std::string fill;
        bool bold = false;
        bool italic = false;
        bool central = false;
    };
    std::vector<Ctx> gStack;
    Ctx root;
    gStack.push_back(root);

    // Bounding boxes of <rect> children, grouped per enclosing
    // <g data="..."> element (diagram exporters tag shape groups
    // this way). Renderer can use the box for label placement.
    struct BoxAcc {
        size_t depth = 0;      // gStack.size() at push time
        bool got = false;      // any rect seen?
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
    };
    struct FinBox { float minX, minY, maxX, maxY; };
    std::vector<FinBox> boxes;   // all data-g boxes seen, doc coords
    std::vector<BoxAcc> boxStack;
    // Start point of every <path id="...">, in that path's own coordinates,
    // for anchoring <textPath>. A textPath carries no x/y of its own, so
    // without this its label has no position at all and is drawn at (0,0).
    struct PathStart { std::string id; float x, y; };
    std::vector<PathStart> pathStarts;
    // Index by id so a <textPath> does not scan every declared path. The scan
    // was quadratic in paths times labels: 5000 of each took 69 ms, 10000 took
    // 277 ms, 20000 took 652 ms and 40000 took 1.71 s. First declaration wins,
    // which is what the scan-and-break did.
    std::unordered_map<std::string, size_t> pathStartById;
    auto AddPathStart = [&](const std::string& id, float x, float y) {
        auto it = pathStartById.find(id);
        if (it != pathStartById.end()) return;
        pathStartById.emplace(id, pathStarts.size());
        pathStarts.push_back({id, x, y});
    };
    StyleSheet styleSheet;   // class rules from <style> blocks

    // First coordinate pair of a path's "d" attribute, which is the start of
    // the first subpath. Handles "M x,y" and "m x,y"; more elaborate path
    // data still yields its first point, which is what textPath needs.
    auto FirstMoveTo = [](const std::string& d, float* ox, float* oy) -> bool {
        size_t i = 0;
        while (i < d.size() && std::isspace(static_cast<unsigned char>(d[i]))) ++i;
        if (i >= d.size() || (d[i] != 'M' && d[i] != 'm')) return false;
        ++i;
        float vals[2] = {0.0f, 0.0f};
        int got = 0;
        while (i < d.size() && got < 2) {
            while (i < d.size() && (d[i] == ',' || d[i] == ' ' ||
                                    d[i] == '\t' || d[i] == '\n')) ++i;
            if (i >= d.size()) break;
            const size_t start = i;
            if (d[i] == '-' || d[i] == '+') ++i;
            while (i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) ||
                                    d[i] == '.' || d[i] == 'e' || d[i] == 'E')) ++i;
            if (i == start) return false;
            try { vals[got++] = std::stof(d.substr(start, i - start)); }
            catch (...) { return false; }
        }
        if (got < 1) return false;
        *ox = vals[0];
        *oy = vals[1];
        return true;
    };

    auto AddRectToBox = [&](const Ctx& m, float lx, float ly,
                            float lw, float lh) {
        if (boxStack.empty()) return;
        float x0 = m.m[0] * lx + m.m[2] * ly + m.m[4];
        float y0 = m.m[1] * lx + m.m[3] * ly + m.m[5];
        float x1 = m.m[0] * (lx + lw) + m.m[2] * (ly + lh) + m.m[4];
        float y1 = m.m[1] * (lx + lw) + m.m[3] * (ly + lh) + m.m[5];
        BoxAcc& b = boxStack.back();
        if (!b.got) {
            b.got = true;
            b.minX = x0 < x1 ? x0 : x1;
            b.maxX = x0 > x1 ? x0 : x1;
            b.minY = y0 < y1 ? y0 : y1;
            b.maxY = y0 > y1 ? y0 : y1;
        } else {
            b.minX = (x0 < x1 ? x0 : x1) < b.minX ? (x0 < x1 ? x0 : x1) : b.minX;
            b.maxX = (x0 > x1 ? x0 : x1) > b.maxX ? (x0 > x1 ? x0 : x1) : b.maxX;
            b.minY = (y0 < y1 ? y0 : y1) < b.minY ? (y0 < y1 ? y0 : y1) : b.minY;
            b.maxY = (y0 > y1 ? y0 : y1) > b.maxY ? (y0 > y1 ? y0 : y1) : b.maxY;
        }
    };

    // Compose: child = parent * op  (apply parent, then op).
    auto ComposeOp = [](float out[6], const float parent[6],
                        const float op[6]) {
        out[0] = parent[0] * op[0] + parent[2] * op[1];
        out[1] = parent[1] * op[0] + parent[3] * op[1];
        out[2] = parent[0] * op[2] + parent[2] * op[3];
        out[3] = parent[1] * op[2] + parent[3] * op[3];
        out[4] = parent[0] * op[4] + parent[2] * op[5] + parent[4];
        out[5] = parent[1] * op[4] + parent[3] * op[5] + parent[5];
    };

    // Parse a transform list; returns composed matrix in the LOCAL
    // (not parent) space. ops: translate/scale/matrix.
    auto ParseTransform = [&](const std::string& tf, float out[6]) {
        out[0] = 1; out[1] = 0; out[2] = 0;
        out[3] = 1; out[4] = 0; out[5] = 0;
        size_t pos = 0;
        while (pos < tf.size()) {
            // Transform names are case-sensitive in SVG ("skewX" is not
            // "skew"), so accept letters in either case and compare against
            // the lowercase spelling.
            size_t nameStart = pos;
            while (pos < tf.size() &&
                   ((tf[pos] >= 'a' && tf[pos] <= 'z') ||
                    (tf[pos] >= 'A' && tf[pos] <= 'Z'))) pos++;
            if (pos == nameStart) { pos++; continue; }
            std::string name = tf.substr(nameStart, pos - nameStart);
            for (char& nc : name) {
                if (nc >= 'A' && nc <= 'Z') nc = static_cast<char>(nc + 32);
            }
            // skip to '('
            while (pos < tf.size() && tf[pos] != 40) pos++;
            if (pos >= tf.size()) break;
            pos++;
            float args[6] = {0, 0, 0, 0, 0, 0};
            int argc = 0;
            std::string num;
            while (pos < tf.size() && tf[pos] != 41) {
                char c = tf[pos];
                if ((c >= 48 && c <= 57) || c == 46 || c == 45 ||
                    c == 43 || c == 101 || c == 69) {
                    num += c;
                } else if (!num.empty()) {
                    if (argc < 6) {
                        try { args[argc++] = std::stof(num); }
                        catch (...) { args[argc++] = 0; }
                    }
                    num.clear();
                }
                pos++;
            }
            if (!num.empty() && argc < 6) {
                try { args[argc++] = std::stof(num); } catch (...) {}
            }
            pos++;  // consume )
            float op[6] = {1, 0, 0, 1, 0, 0};
            if (name == "translate") {
                op[4] = args[0];
                op[5] = (argc > 1) ? args[1] : 0;
            } else if (name == "scale") {
                op[0] = args[0];
                op[3] = (argc > 1) ? args[1] : args[0];
            } else if (name == "matrix" && argc >= 6) {
                for (int mi = 0; mi < 6; mi++) op[mi] = args[mi];
            } else if (name == "rotate" && argc >= 1) {
                // rotate(a) spins about the origin. rotate(a cx cy) spins
                // about (cx,cy), which is translate(cx,cy) rotate(a)
                // translate(-cx,-cy) collapsed into one matrix here.
                const float rad = args[0] * 3.14159265358979f / 180.0f;
                const float cs = std::cos(rad), sn = std::sin(rad);
                op[0] = cs;  op[1] = sn;
                op[2] = -sn; op[3] = cs;
                if (argc >= 3) {
                    const float cx = args[1], cy = args[2];
                    op[4] = cx - cx * cs + cy * sn;
                    op[5] = cy - cx * sn - cy * cs;
                }
            } else if (name == "skewx" && argc >= 1) {
                op[2] = std::tan(args[0] * 3.14159265358979f / 180.0f);
            } else if (name == "skewy" && argc >= 1) {
                op[1] = std::tan(args[0] * 3.14159265358979f / 180.0f);
            } else { continue; }
            float temp[6];
            ComposeOp(temp, out, op);
            for (int mi = 0; mi < 6; mi++) out[mi] = temp[mi];
        }
    };

    // Presentation properties that inherit down the tree. Reading them in one
    // place keeps <g>, <svg>, <text> and <tspan> from drifting apart. The root
    // <svg> carries font-family on most exporters (Batik, Graphviz, hand
    // written files), and ignoring it there made every label fall back to the
    // app's UI font instead of the family the drawing asks for.
    auto ApplyInherited = [&](Ctx& c, const std::string& tag) {
        std::string v;
        v = GetAttrOrStyle(tag, "font-size", &styleSheet);
        if (!v.empty()) {
            // ToFloat stops at the px/pt suffix; scale by the freshly
            // composed vertical scale factor.
            c.fontSize = ToFloat(v, c.fontSize) * c.m[3];
        }
        v = GetAttrOrStyle(tag, "font-family", &styleSheet);
        if (!v.empty()) c.fontFamily = v;
        v = GetAttrOrStyle(tag, "text-anchor", &styleSheet);
        if (!v.empty()) c.anchor = v;
        v = GetAttrOrStyle(tag, "fill", &styleSheet);
        if (!v.empty() && v != "none") c.fill = v;
        v = GetAttrOrStyle(tag, "font-weight", &styleSheet);
        if (!v.empty()) c.bold = (v == "bold" || v == "700" || v == "bolder");
        v = GetAttrOrStyle(tag, "font-style", &styleSheet);
        if (!v.empty()) c.italic = (v == "italic" || v == "oblique");
    };

    int defsDepth = 0;
    // <switch> renders its first child whose requiredFeatures/
    // requiredExtensions/systemLanguage all pass. We cannot evaluate those
    // tests, so we take the first child and skip the rest; drawing every
    // branch stamps each fallback label on top of the others.
    // Skipping a <switch> fallback means skipping a whole subtree, closing tag
    // included, so the depth counter must distinguish the three situations.
    // Collapsing "waiting for the first child" into "inside the chosen child"
    // made the first branch get skipped and the second drawn, which is the
    // opposite of what <switch> means.
    const int kSwWantChild = -1;   // inside a switch, chosen branch not entered
    const int kSwInChosen  = -2;   // inside the chosen branch, drawing it
    const int kSwAfterBranch = -3; // chosen branch done, skipping siblings
    int chosenDepth = 0;         // nesting depth within the chosen element
    bool chosenWasText = false;   // the chosen branch is a bare <text>
    int switchDepthSave = 0;     // chosenDepth to resume after a nested switch
    int switchSkip = 0;            // 0 = outside; >0 = depth inside a fallback
    int switchSkipRestore = 0;    // branch state to resume after a nested switch
    bool chainNext = false; // set by a tspan carrying its own x
    bool tspanHadX = false;  // the tspan currently open set its own x

    while (true) {
        std::string tag = p.NextTag();
        if (tag.empty()) break;

        if (TagIs(tag, "defs")) {
            if (tag.size() > 2 && tag[1] != 47) {
                // Self-closing <defs .../> opens and closes at once.
                // tag.back() is '>', check the char before it.
                bool selfClosed = tag[tag.size() - 2] == 47;
                if (!selfClosed) defsDepth++;
            } else {
                defsDepth = (defsDepth > 0) ? defsDepth - 1 : 0;
            }
            continue;
        }
        if (defsDepth > 0) {
            // <defs> holds no renderable content, but it is exactly where a
            // reusable <path id="..."> lives for a <textPath> to reference.
            // Skipping it wholesale left such labels anchored at (0,0).
            if (TagIs(tag, "path") && tag.size() > 1 && tag[1] != 47) {
                const std::string pid = GetAttrOrStyle(tag, "id");
                const std::string dd = GetAttrOrStyle(tag, "d");
                float px = 0.0f, py = 0.0f;
                if (!pid.empty() && !dd.empty() && FirstMoveTo(dd, &px, &py)) {
                    AddPathStart(pid, px, py);
                }
            }
            continue;
        }

        // <switch> renders its first child whose feature tests pass. We cannot
        // evaluate systemLanguage or requiredExtensions, so we take the first
        // child and skip the rest. Drawing every branch stamps each fallback
        // label directly on top of the others.
        // <style> holds CSS, not content. Collect its rules so class-based
        // styling applies, then skip the body. Hand-authored and Inkscape
        // files rely on this heavily.
        if (TagIs(tag, "style") && tag.size() > 1 && tag[1] != 47) {
            // The body may be plain text or wrapped in CDATA.
            std::string cssBody;
            if (p.AtCData()) cssBody = p.TakeCData();
            else cssBody = p.TextUntilTag();
            const StyleSheet sheet = ParseStyleSheet(cssBody);
            // Go through Add so byName is rebuilt as well: a later <style>
            // block must not displace an earlier declaration for the same
            // class, which is the cascade order the scan gave us.
            for (const auto& r : sheet.rules) styleSheet.Add(r.first, r.second);
            continue;
        }

        // <switch> draws its first child whose feature tests pass; the rest
        // are fallbacks. Drawing every branch stamps each fallback label
        // directly on top of the others at the same coordinates. We cannot
        // evaluate systemLanguage or requiredExtensions, so the first child
        // with content wins and the remainder is skipped whole.
        //
        // The decision must not consume the chosen element: it is usually a
        // <g> whose transform still has to be pushed, and any leaf inside it
        // (a <text>, a <rect>) still has to be drawn. So this only updates
        // state and marks whether the tag itself is discarded.
        bool skipThisTag = false;
        if (switchSkip != 0) {
            const bool isClose = tag.size() > 1 && tag[1] == 47;
            const bool selfClosed = IsSelfClosed(tag);
            if (TagIs(tag, "/switch") && switchSkipRestore != 0) {
                // A nested switch closed, so the branch that held it is
                // finished: everything after it is a fallback of the enclosing
                // switch. Restoring "inside the chosen branch" here instead let
                // the outer fallback draw, because the chosen element was the
                // nested switch itself and its depth had already unwound.
                switchSkip = kSwAfterBranch;
                switchSkipRestore = 0;
                chosenDepth = 0;
                switchDepthSave = 0;
                continue;
            }
            if (switchSkip == kSwWantChild) {
                if (isClose || selfClosed) {
                    skipThisTag = true;
                } else if (TagIs(tag, "switch")) {
                    // A nested switch decides for itself; resume this branch
                    // when it closes.
                    switchSkipRestore = kSwInChosen;
                    switchDepthSave = chosenDepth;
                    skipThisTag = true;
                } else {
                    // This element is the chosen branch: handle it normally
                    // below and draw its subtree at depth 1.
                    switchSkip = kSwInChosen;
                    chosenDepth = 1;
                    chosenWasText = TagIs(tag, "text");
                }
            } else if (switchSkip == kSwInChosen) {
                // Inside the chosen branch. A nested switch re-decides; any
                // other open tag nests one level. Nothing is discarded, so the
                // leaf handlers below still see <text>, <rect> and friends.
                // A <text> subtree is consumed whole by the inner text loop,
                // so counting it here would raise the depth that its own
                // </text> never lowers, and the branch would never close.
                // A self-closing element has no matching close tag, so it
                // must not raise the depth: counting <rect/> left the depth
                // one too high and the branch never ended.
                const bool countsAsLevel = !TagIs(tag, "text") && !selfClosed;
                if (!isClose && !selfClosed) {
                    if (TagIs(tag, "switch")) {
                        switchSkipRestore = kSwInChosen;
                        switchDepthSave = chosenDepth;
                        switchSkip = kSwWantChild;
                        skipThisTag = true;
                    } else if (countsAsLevel) {
                        ++chosenDepth;
                    }
                } else if (isClose) {
                    // The chosen element's own close ends the branch, so its
                    // following siblings are fallbacks. Not while a nested
                    // switch is pending: that switch owns the branch until its
                    // own </switch> hands control back, and ending it here would
                    // discard the nested choice.
                    if (!TagIs(tag, "/text") && --chosenDepth == 0 &&
                        switchSkipRestore == 0) {
                        switchSkip = kSwAfterBranch;
                    }
                }
            } else if (switchSkip == kSwAfterBranch) {
                // Between branches: skip everything up to this </switch>.
                if (TagIs(tag, "/switch")) switchSkip = 0;
                else if (!isClose && !selfClosed) switchSkip = 1;
                continue;
            } else {
                // Inside a skipped fallback: track nesting to skip it whole.
                if (!isClose && !selfClosed) ++switchSkip;
                else if (isClose) --switchSkip;
                continue;
            }
        }
        if (skipThisTag) continue;
        if (TagIs(tag, "/switch")) { switchSkip = 0; continue; }
        if (TagIs(tag, "switch") && tag.size() > 1 && tag[1] != 47) {
            // NextTag() keeps the '>', so the self-closing slash is the
            // character before it. Testing tag.back() here was always true,
            // which put a self-closed <switch/> into the waiting state and
            // made it swallow the rest of the document.
            if (!IsSelfClosed(tag)) switchSkip = kSwWantChild;
            continue;
        }

        // <g> open/close: transform + inheritable style props.
        if (TagIs(tag, "g") && tag.size() > 1 && tag[1] != 47) {
            // Diagram exporters mark shape groups with data="...";
            // rect bounds inside become the label box.
            if (!GetAttrOrStyle(tag, "data").empty()) {
                BoxAcc acc;
                acc.depth = gStack.size();  // current depth BEFORE push
                boxStack.push_back(acc);
            }
            Ctx child = gStack.back();
            std::string tf = GetAttrOrStyle(tag, "transform");
            if (!tf.empty() && tf != "none") {
                float composed[6];
                ParseTransform(tf, composed);
                float out2[6];
                ComposeOp(out2, child.m, composed);
                for (int mi = 0; mi < 6; mi++) child.m[mi] = out2[mi];
            }
            ApplyInherited(child, tag);
            gStack.push_back(child);
            // Self-closing <g/>: open and close at once. The slash is the
            // character before the '>', never tag.back(). When this test was
            // permanently false every <g/> leaked a level, so its transform
            // was inherited by all later siblings and the stack grew with
            // the input.
            if (IsSelfClosed(tag)) gStack.pop_back();
            // Self-closing data-g: close the box right away? Runs
            // come after; a self-closing shape group holds no text,
            // so just drop an empty accumulator.
            if (IsSelfClosed(tag) && !boxStack.empty()) {
                boxStack.pop_back();
            }
            continue;
        }
        if (TagIs(tag, "/g")) {
            if (gStack.size() > 1) gStack.pop_back();
            // Close every box whose data-g is at or above the new
            // depth (nested unmarked <g></g> pairs do not close it).
            while (!boxStack.empty() &&
                   boxStack.back().depth >= gStack.size()) {
                BoxAcc b = boxStack.back();
                boxStack.pop_back();
                if (b.got) {
                    boxes.push_back({ b.minX, b.minY, b.maxX, b.maxY });
                }
            }
            continue;
        }

        // A nested <svg x=".." y=".."> shifts its whole subtree. Treat it as
        // a translate so labels inside a nested viewport do not land at the
        // parent's origin.
        if (TagIs(tag, "svg") && tag.size() > 1 && tag[1] != 47) {
            Ctx child = gStack.back();
            const std::string nx = GetAttrOrStyle(tag, "x");
            const std::string ny = GetAttrOrStyle(tag, "y");
            if (!nx.empty() || !ny.empty()) {
                float op[6] = {1, 0, 0, 1, 0, 0};
                op[4] = nx.empty() ? 0.0f : ToFloat(nx, 0.0f);
                op[5] = ny.empty() ? 0.0f : ToFloat(ny, 0.0f);
                float out2[6];
                ComposeOp(out2, child.m, op);
                for (int mi = 0; mi < 6; mi++) child.m[mi] = out2[mi];
            }
            // The root <svg> is where most exporters put font-family, and a
            // nested one may carry its own. Both inherit like a <g>.
            ApplyInherited(child, tag);
            gStack.push_back(child);
            if (IsSelfClosed(tag)) gStack.pop_back();
            continue;
        }
        if (TagIs(tag, "/svg")) {
            if (gStack.size() > 1) gStack.pop_back();
            continue;
        }

        // <path id=".."> records a start point for <textPath> anchoring.
        // Collected before the defs skip would matter, but paths used by a
        // textPath are normally declared outside <defs> too, and this branch
        // runs for any <path> the scanner reaches.
        if (TagIs(tag, "path") && tag.size() > 1 && tag[1] != 47) {
            const std::string pid = GetAttrOrStyle(tag, "id");
            const std::string dd = GetAttrOrStyle(tag, "d");
            float px = 0.0f, py = 0.0f;
            if (!pid.empty() && !dd.empty() && FirstMoveTo(dd, &px, &py)) {
                AddPathStart(pid, px, py);
            }
            continue;
        }

        // <rect ...> feeds the enclosing data-g bounding box.
        if (TagIs(tag, "rect") && tag.size() > 1 && tag[1] != 47 &&
            !boxStack.empty()) {
            Ctx cur = gStack.back();
            std::string rv = GetAttrOrStyle(tag, "x");
            float lx = rv.empty() ? 0.0f : ToFloat(rv, 0.0f);
            rv = GetAttrOrStyle(tag, "y");
            float ly = rv.empty() ? 0.0f : ToFloat(rv, 0.0f);
            rv = GetAttrOrStyle(tag, "width");
            float lw = rv.empty() ? 0.0f : ToFloat(rv, 0.0f);
            rv = GetAttrOrStyle(tag, "height");
            float lh = rv.empty() ? 0.0f : ToFloat(rv, 0.0f);
            if (lw != 0.0f && lh != 0.0f) {
                AddRectToBox(cur, lx, ly, lw, lh);
            }
            continue;
        }

        // Opening <text ...>
        if (TagIs(tag, "text") && tag.size() > 1 && tag[1] != 47) {
            Ctx ctx = gStack.back();
            // First x/y value of possibly space-separated lists.
            auto firstNum = [](const std::string& in, float def) -> float {
                float outv = def;
                size_t k = 0;
                while (k < in.size() && !(in[k] >= 48 && in[k] <= 57) &&
                       in[k] != 45 && in[k] != 46) k++;
                if (k < in.size()) {
                    std::string num;
                    while (k < in.size() && ((in[k] >= 48 && in[k] <= 57) ||
                           in[k] == 45 || in[k] == 46 || in[k] == 101 ||
                           in[k] == 43)) { num += in[k]; k++; }
                    try { outv = std::stof(num); } catch (...) {}
                }
                return outv;
            };
            ApplyInherited(ctx, tag);
            // dominant-baseline / alignment-baseline modes that make
            // y the vertical center of the glyphs.
            std::string db = GetAttrOrStyle(tag, "dominant-baseline", &styleSheet);
            if (db.empty()) db = GetAttrOrStyle(tag, "alignment-baseline", &styleSheet);
            if (db == "central" || db == "middle") ctx.central = true;
            float xBase = firstNum(GetAttrOrStyle(tag, "x", &styleSheet), 0.0f);
            float yBase = firstNum(GetAttrOrStyle(tag, "y", &styleSheet), 0.0f);
            // dy with no y is the only way to place such text: Batik and
            // Graphviz emit <text x="10" dy="12">. Ignoring it drops the
            // label onto the top edge of the drawing.
            const std::string dyStr = GetAttrOrStyle(tag, "dy", &styleSheet);
            if (!dyStr.empty() && GetAttrOrStyle(tag, "y", &styleSheet).empty()) {
                yBase = firstNum(dyStr, yBase);
            }
            // <text><textPath href="#id"> carries no x/y of its own, so its
            // label had no position and was drawn at the document origin, on
            // top of unrelated content. Anchor at the start of the referenced
            // path instead. Start is an approximation (a faithful version
            // measures along the curve and honours startOffset), but it puts
            // the label on its path rather than at 0,0.
            bool onTextPath = false;
            {
                // A <textPath> is the first child of its <text>, so peek at
                // it without consuming: look for the tag right after this
                // <text>'s own attributes.
                const std::string probe = p.PeekTag();
                if (TagIs(probe, "textpath")) {
                    std::string href = GetAttrOrStyle(probe, "href");
                    if (href.empty()) href = GetAttrOrStyle(probe, "xlink:href");
                    std::string wanted = href;
                    if (!wanted.empty() && wanted[0] == '#')
                        wanted = wanted.substr(1);
                    auto pit = pathStartById.find(wanted);
                    if (pit != pathStartById.end()) {
                        const PathStart& ps = pathStarts[pit->second];
                        // Compose the path's start through the current
                        // transform so nested groups place it correctly.
                        const float lx = ctx.m[0] * ps.x + ctx.m[2] * ps.y + ctx.m[4];
                        const float ly = ctx.m[1] * ps.x + ctx.m[3] * ps.y + ctx.m[5];
                        xBase = lx;
                        yBase = ly;
                        onTextPath = true;
                    }
                }
            }

            // The text element itself is a level on the stack so the
            // inner tspan loop and the run builder read one context.
            gStack.push_back(ctx);

            while (true) {
                // CDATA inside <text> is literal content, not markup, so it
                // becomes a run instead of being skipped as a tag.
                if (p.AtCData()) {
                    const std::string lit = p.TakeCData();
                    if (!lit.empty()) {
                        Ctx use = gStack.back();
                        TextRun cr;
                        cr.x = use.m[0] * xBase + use.m[2] * yBase + use.m[4];
                        cr.y = use.m[1] * xBase + use.m[3] * yBase + use.m[5];
                        cr.fontSize = use.fontSize;
                        cr.fontFamily = use.fontFamily;
                        cr.anchor = use.anchor;
                        cr.fill = use.fill;
                        cr.bold = use.bold;
                        cr.italic = use.italic;
                        cr.central = use.central;
                        cr.chained = chainNext;
                        chainNext = false;
                        cr.text = lit;
                        runs.push_back(cr);
                    }
                    continue;
                }
                // Text between tags is the run content.
                {
                    const std::string raw = p.TextUntilTag();
                    const size_t a = raw.find_first_not_of(" \t\n\r");
                    const size_t z = raw.find_last_not_of(" \t\n\r");
                    if (a != std::string::npos && z != std::string::npos && z >= a) {
                        Ctx use = gStack.back();
                        TextRun run;
                        run.x = use.m[0] * xBase + use.m[2] * yBase + use.m[4];
                        run.y = use.m[1] * xBase + use.m[3] * yBase + use.m[5];
                        run.fontSize = use.fontSize;
                        run.fontFamily = use.fontFamily;
                        run.anchor = use.anchor;
                        run.fill = use.fill;
                        run.bold = use.bold;
                        run.italic = use.italic;
                        run.central = use.central;
                        // A rotate or skew travels with the run so the
                        // renderer turns the glyphs with their shape.
                        if (use.m[0] != 1.0f || use.m[1] != 0.0f ||
                            use.m[2] != 0.0f || use.m[3] != 1.0f) {
                            run.transformed = true;
                            run.ma = use.m[0]; run.mb = use.m[1];
                            run.mc = use.m[2]; run.md = use.m[3];
                        }
                        run.chained = chainNext;
                        chainNext = false;
                        run.text = DecodeEntities(raw.substr(a, z - a + 1));
                        runs.push_back(run);
                    }
                }
                std::string inner = p.NextTag();
                if (inner.empty()) {
                    if (gStack.size() > 1) gStack.pop_back();
                    // Only when this <text> was itself the branch the switch
                    // chose. A <text> nested inside a chosen <g> must not end
                    // the branch, or the rest of that group is skipped.
                    if (chosenWasText && switchSkip == kSwInChosen) {
                        switchSkip = kSwAfterBranch;
                        chosenWasText = false;
                    }
                    break;
                }
                if (TagIs(inner, "/text")) {
                    if (gStack.size() > 1) gStack.pop_back();
                    if (chosenWasText && switchSkip == kSwInChosen) {
                        switchSkip = kSwAfterBranch;
                        chosenWasText = false;
                    }
                    break;
                }
                // Skip the <textPath> wrapper itself: it carries the anchor
                // we already applied, and its own content is not a run.
                if (TagIs(inner, "textpath")) {
                    if (inner.size() > 1 && inner[1] == 47) {
                        // The opening <textPath> deliberately pushed nothing, so
                        // its close must pop nothing. Popping here consumed the
                        // <text> level and then the enclosing group's, so every
                        // later sibling in that group lost its transform.
                        continue;
                    }
                    if (inner.back() == 47) continue;  // empty, self-closed
                    continue;
                }
                if (TagIs(inner, "tspan") && inner.size() > 1 && inner[1] != 47) {
                    Ctx child = gStack.back();
                    std::string v2;
                    v2 = GetAttrOrStyle(inner, "x", &styleSheet);
                    if (!v2.empty()) {
                        xBase = firstNum(v2, xBase);
                        // An explicit x repositions the pen, so this tspan's
                        // text continues from the previous run instead of
                        // restarting at xBase.
                        chainNext = true;
                        tspanHadX = true;
                    }
                    v2 = GetAttrOrStyle(inner, "y", &styleSheet);
                    std::string yv = GetAttrOrStyle(inner, "y", &styleSheet);
                    if (!yv.empty()) yBase = firstNum(yv, yBase);
                    ApplyInherited(child, inner);
                    gStack.push_back(child);
                    if (IsSelfClosed(inner)) gStack.pop_back();
                    continue;
                }
                if (TagIs(inner, "/tspan")) {
                    if (gStack.size() > 1) gStack.pop_back();
                    // Text after the tspan continues from the tspan's end, so
                    // it is chained too when that tspan carried its own x.
                    chainNext = tspanHadX;
                    tspanHadX = false;
                    continue;
                }
            }
            continue;
        }
    }

    // Pair each run with its box: Batik/Archi exporters put the
    // label groups SIBLING to the shape group, so the enclosing
    // data-g at text time is not the label box. The smallest
    // box containing the run anchor wins; otherwise the box
    // whose center is nearest the run anchor.
    //
    // Scanning every box for every run is O(runs * boxes), which is quadratic:
    // a diagram with 2000 shapes and 2000 labels spent 24 ms, but 8000 of
    // each took 307 ms, and the growth is 3.8x per doubling of both. That is
    // a hang on a large but entirely ordinary diagram, so the boxes go into
    // a uniform grid first and each run only visits the cells it can reach.
    //
    // Containment and nearest-centre both need every box whose extent covers
    // the run anchor, so a box is filed into all the cells it overlaps and
    // searched through those. The nearest-centre fallback still has to see
    // every box in the worst case (a run outside all cells), so it is
    // computed over the same candidate set: any box closer than the best
    // candidate found must overlap a cell the run visited, because a box
    // containing or surrounding the anchor always overlaps its cell.
    if (!boxes.empty()) {
        // One cell per ~64 units of extent, clamped so a tiny or huge drawing
        // cannot produce a useless or enormous grid.
        float minX = boxes[0].minX, maxX = boxes[0].maxX;
        float minY = boxes[0].minY, maxY = boxes[0].maxY;
        for (const auto& b : boxes) {
            if (b.minX < minX) minX = b.minX;
            if (b.maxX > maxX) maxX = b.maxX;
            if (b.minY < minY) minY = b.minY;
            if (b.maxY > maxY) maxY = b.maxY;
        }
        const float spanX = (maxX - minX);
        const float spanY = (maxY - minY);
        size_t cols = static_cast<size_t>(spanX / 64.0f) + 1;
        size_t rows = static_cast<size_t>(spanY / 64.0f) + 1;
        // Cap the cell count: past this point a linear scan over a bucket is
        // no worse than the grid lookup, and the memory stops being free.
        constexpr size_t kMaxCells = 1u << 16;
        while (cols * rows > kMaxCells && (cols > 1 || rows > 1)) {
            if (cols >= rows && cols > 1) cols = (cols + 1) / 2;
            else if (rows > 1) rows = (rows + 1) / 2;
        }
        const float cellW = spanX / static_cast<float>(cols) + 1e-6f;
        const float cellH = spanY / static_cast<float>(rows) + 1e-6f;
        auto colOf = [&](float x) -> long {
            long c = static_cast<long>((x - minX) / cellW);
            if (c < 0) c = 0;
            if (c >= static_cast<long>(cols)) c = static_cast<long>(cols) - 1;
            return c;
        };
        auto rowOf = [&](float y) -> long {
            long r = static_cast<long>((y - minY) / cellH);
            if (r < 0) r = 0;
            if (r >= static_cast<long>(rows)) r = static_cast<long>(rows) - 1;
            return r;
        };

        std::vector<std::vector<const FinBox*>> grid(cols * rows);
        for (const auto& b : boxes) {
            const long c0 = colOf(b.minX), c1 = colOf(b.maxX);
            const long r0 = rowOf(b.minY), r1 = rowOf(b.maxY);
            for (long r = r0; r <= r1; ++r) {
                for (long c = c0; c <= c1; ++c) {
                    grid[static_cast<size_t>(r * static_cast<long>(cols) + c)]
                        .push_back(&b);
                }
            }
        }

        for (auto& run : runs) {
            const FinBox* contain = nullptr;
            float bestArea = 0.0f;
            size_t bestContainIdx = 0;
            const FinBox* nearest = nullptr;
            float bestDist = 0.0f;
            size_t bestNearIdx = 0;
            const long cr = rowOf(run.y);
            const long cc = colOf(run.x);
            // Visit cells in expanding square rings around the run and stop as
            // soon as no further ring can hold a closer centre. Containment
            // and nearest-centre are both decided within a bounded radius, so
            // the result matches the old full scan exactly while touching only
            // a handful of cells instead of every box.
            auto consider = [&](const FinBox* bp) {
                const FinBox& b = *bp;
                float plo = (b.minX < b.maxX) ? b.minX : b.maxX;
                float phi = (b.minX < b.maxX) ? b.maxX : b.minX;
                float qlo = (b.minY < b.maxY) ? b.minY : b.maxY;
                float qhi = (b.minY < b.maxY) ? b.maxY : b.minY;
                bool inside = run.x >= plo && run.x <= phi &&
                              run.y >= qlo && run.y <= qhi;
                float area = (phi - plo) * (qhi - qlo);
                float cx = (b.minX + b.maxX) * 0.5f;
                float cy = (b.minY + b.maxY) * 0.5f;
                float dx = run.x - cx;
                float dy = run.y - cy;
                float dist = dx * dx + dy * dy;
                // The grid visits boxes in a different order than the old
                // full scan, so a tie must be broken on the box's own
                // position in `boxes`, not on which one happened to be seen
                // first. Without this, two equally good candidates swapped
                // places and the rendered label moved to a different shape.
                const size_t idx = static_cast<size_t>(bp - boxes.data());
                if (inside && (!contain || area < bestArea ||
                               (area == bestArea && idx < bestContainIdx))) {
                    contain = &b;
                    bestArea = area;
                    bestContainIdx = idx;
                }
                if (!nearest || dist < bestDist ||
                    (dist == bestDist && idx < bestNearIdx)) {
                    nearest = &b;
                    bestDist = dist;
                    bestNearIdx = idx;
                }
            };
            const long maxRing = static_cast<long>(
                std::max(cols, rows));
            for (long ring = 0; ring <= maxRing; ++ring) {
                // After this ring, every cell that has not been visited yet is
                // at least `ring` cells away, so its NEAR EDGE is at least
                // ring * cell from the run's own cell edge. A box centre lies
                // inside its box, so it cannot be nearer than that. Once both
                // axes are beyond the best distance found, no later ring can
                // improve on it.
                //
                // The bound is deliberately loose by one cell: a run sitting
                // anywhere inside its own cell can be almost a full cell away
                // from the near edge of the next ring. Using the near edge of
                // the NEXT ring (ring, not ring+1) is what keeps the search
                // exhaustive; an earlier version stopped one ring too soon and
                // picked the wrong shape for runs near a cell boundary.
                //
                // Only meaningful once a box has actually been seen: bestDist is
                // 0 until then, which would otherwise read as "already
                // unbeatable" and abandon the search before any candidate.
                if (ring > 0 && nearest) {
                    const float reachX =
                        static_cast<float>(ring - 1) * cellW;
                    const float reachY =
                        static_cast<float>(ring - 1) * cellH;
                    if (reachX * reachX >= bestDist &&
                        reachY * reachY >= bestDist) {
                        break;
                    }
                }
                const long r0 = cr - ring, r1 = cr + ring;
                const long c0 = cc - ring, c1 = cc + ring;
                for (long r = r0; r <= r1; ++r) {
                    if (r < 0 || r >= static_cast<long>(rows)) continue;
                    const bool edgeRow = (r == r0 || r == r1);
                    for (long c = c0; c <= c1; ++c) {
                        if (c < 0 || c >= static_cast<long>(cols)) continue;
                        // The interior of a thick ring was already done.
                        if (ring > 0 && !edgeRow && c != c0 && c != c1) continue;
                        for (const FinBox* bp :
                             grid[static_cast<size_t>(
                                 r * static_cast<long>(cols) + c)]) {
                            consider(bp);
                        }
                    }
                }
            }
            const FinBox* chosen = contain ? contain : nearest;
            if (chosen) {
                run.boxValid = true;
                run.bx = chosen->minX;
                run.by = chosen->minY;
                run.bw = chosen->maxX - chosen->minX;
                run.bh = chosen->maxY - chosen->minY;
            }
        }
    }

    return runs;
}

std::string StripTextElements(const std::string& xml) {
    std::string out;
    out.reserve(xml.size());
    XmlPos p(xml);
    int stripDepth = 0; // nesting depth inside text/tspan/foreignObject

    // We also need to handle text that appears outside tags
    while (true) {
        // Emit text up to next '<' if not stripping
        size_t lt = xml.find('<', p.pos);
        if (lt == std::string::npos) {
            if (stripDepth == 0) out += xml.substr(p.pos);
            p.pos = xml.size();
            break;
        }
        if (stripDepth == 0) {
            out += xml.substr(p.pos, lt - p.pos);
        }
        p.pos = lt;

        // Find end of tag
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string::npos) {
            if (stripDepth == 0) out += xml.substr(lt);
            p.pos = xml.size();
            break;
        }

        std::string tag = xml.substr(lt, gt - lt + 1);
        p.pos = gt + 1;

        bool isOpen = tag.size() > 1 && tag[1] != '/';
        bool isText = TagIs(tag, "text") || TagIs(tag, "tspan") || TagIs(tag, "foreignObject");

        if (stripDepth > 0) {
            // We are inside a stripped element
            if (isOpen && isText && !IsSelfClosed(tag)) stripDepth++;
            else if (!isOpen) {
                // Closing tag
                if (isText || TagIs(tag, "/text") || TagIs(tag, "/tspan")
                    || TagIs(tag, "/foreignObject")) {
                    stripDepth--;
                }
            }
        } else {
            if (isOpen && isText) {
                if (IsSelfClosed(tag)) {
                    // Self-closing, nothing to strip
                } else {
                    stripDepth = 1;
                }
            } else {
                out += tag;
            }
        }
    }

    return out;
}


// --- Marker expansion ---------------------------------------------------
//
// Direct2D's SVG renderer supports neither <marker> nor the marker-start /
// marker-end presentation attributes (the documented element and attribute
// lists stop short of them), so every arrowhead in a diagram was silently
// dropped. A marker is a pure placement problem: the referenced content is
// drawn in a coordinate system translated to the path end, rotated to the
// path direction, scaled by the stroke width and shifted by refX/refY.
// Rewriting each reference into an explicit <g transform="..."> around the
// marker's own content gives Direct2D something it can draw, and keeps the
// geometry in the document where the browser puts it.

namespace {

struct MarkerDef {
    std::string id;
    float markerW = 3.0f;
    float markerH = 3.0f;
    float refX = 0.0f;
    float refY = 0.0f;
    bool strokeUnits = true;   // markerUnits="strokeWidth" is the default
    std::string orient;        // "auto", "auto-start-reverse", or an angle
    std::string transform;     // the marker's own transform, if any
    bool hasViewBox = false;
    float vbX = 0.0f, vbY = 0.0f, vbW = 0.0f, vbH = 0.0f;
    std::string content;       // inner XML, copied verbatim
};

struct PathGeom {
    bool hasStart = false;
    bool hasStartDir = false;
    bool hasEnd = false;
    float sx = 0.0f, sy = 0.0f;
    float sdx = 0.0f, sdy = 0.0f;
    float ex = 0.0f, ey = 0.0f;
    float edx = 0.0f, edy = 0.0f;
};

// Reads one SVG number at *i, skipping separators. Returns false at the end
// of the data or at a command letter, and leaves *i untouched then.
bool ReadNumber(const std::string& s, size_t* i, float* out) {
    size_t p = *i;
    while (p < s.size() && (s[p] == ',' || s[p] == ' ' || s[p] == '\t' ||
                            s[p] == '\n' || s[p] == '\r')) ++p;
    const size_t start = p;
    if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
    bool digits = false;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
        ++p;
        digits = true;
    }
    if (p < s.size() && s[p] == '.') {
        ++p;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
            ++p;
            digits = true;
        }
    }
    if (!digits) return false;
    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
        const size_t save = p;
        ++p;
        if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
        bool expDigits = false;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
            ++p;
            expDigits = true;
        }
        if (!expDigits) p = save;
    }
    try {
        *out = std::stof(s.substr(start, p - start));
    } catch (...) {
        return false;
    }
    *i = p;
    return true;
}

// Normalised direction from (x0,y0) to (x1,y1). A zero-length segment
// reports false so the caller keeps the previous direction: a marker on a
// degenerate end should keep pointing where the path was going.
bool UnitDir(float x0, float y0, float x1, float y1, float* dx, float* dy) {
    const float vx = x1 - x0, vy = y1 - y0;
    const float len = std::sqrt(vx * vx + vy * vy);
    if (!(len > 1e-6f)) return false;
    *dx = vx / len;
    *dy = vy / len;
    return true;
}

// Records one segment. srx/sry is a point that, with (x0,y0), gives the
// tangent at the start; erx/ery does the same for the end. For a straight
// line both are simply the other end of the segment.
void AddSegment(PathGeom* g, float x0, float y0, float x1, float y1,
                float srx, float sry, float erx, float ery) {
    if (!g->hasStart) {
        g->sx = x0;
        g->sy = y0;
        g->hasStart = true;
    }
    if (!g->hasStartDir) {
        float ux = 0.0f, uy = 0.0f;
        if (UnitDir(x0, y0, srx, sry, &ux, &uy)) {
            g->sdx = ux;
            g->sdy = uy;
            g->hasStartDir = true;
        }
    }
    g->ex = x1;
    g->ey = y1;
    float ux = 0.0f, uy = 0.0f;
    if (UnitDir(erx, ery, x1, y1, &ux, &uy)) {
        g->edx = ux;
        g->edy = uy;
    }
    g->hasEnd = true;
}

// Walks a path's "d" attribute and records its start point, start tangent,
// end point and end tangent. Curves use their control points, which is the
// exact tangent; an arc uses its chord, which is exact for the quarter turns
// diagram exporters emit and close enough elsewhere.
void ParsePathGeometry(const std::string& d, PathGeom* g) {
    size_t i = 0;
    float cx = 0.0f, cy = 0.0f;      // current point
    float subX = 0.0f, subY = 0.0f;  // current subpath start
    char cmd = 0;

    while (i < d.size()) {
        while (i < d.size() && (d[i] == ',' || d[i] == ' ' || d[i] == '\t' ||
                                d[i] == '\n' || d[i] == '\r')) ++i;
        if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) {
            cmd = d[i];
            ++i;
        } else if (!cmd) {
            ++i;
            continue;
        }
        float a = 0.0f, b = 0.0f, c = 0.0f, e = 0.0f, f = 0.0f, h = 0.0f;
        switch (cmd) {
        case 'M': case 'm': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b)) { i = d.size(); break; }
            if (cmd == 'm') { a += cx; b += cy; }
            cx = a; cy = b;
            subX = a; subY = b;
            if (!g->hasStart) { g->sx = a; g->sy = b; g->hasStart = true; }
            // Further coordinate pairs after a moveto are implicit linetos.
            cmd = (cmd == 'M') ? 'L' : 'l';
            break;
        }
        case 'L': case 'l': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b)) { i = d.size(); break; }
            if (cmd == 'l') { a += cx; b += cy; }
            AddSegment(g, cx, cy, a, b, a, b, cx, cy);
            cx = a; cy = b;
            break;
        }
        case 'H': case 'h': {
            if (!ReadNumber(d, &i, &a)) { i = d.size(); break; }
            if (cmd == 'h') a += cx;
            AddSegment(g, cx, cy, a, cy, a, cy, cx, cy);
            cx = a;
            break;
        }
        case 'V': case 'v': {
            if (!ReadNumber(d, &i, &a)) { i = d.size(); break; }
            if (cmd == 'v') a += cy;
            AddSegment(g, cx, cy, cx, a, cx, a, cx, cy);
            cy = a;
            break;
        }
        case 'C': case 'c': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b) ||
                !ReadNumber(d, &i, &c) || !ReadNumber(d, &i, &e) ||
                !ReadNumber(d, &i, &f) || !ReadNumber(d, &i, &h)) { i = d.size(); break; }
            if (cmd == 'c') { a += cx; b += cy; c += cx; e += cy; f += cx; h += cy; }
            AddSegment(g, cx, cy, f, h, a, b, c, e);
            cx = f; cy = h;
            break;
        }
        case 'S': case 's': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b) ||
                !ReadNumber(d, &i, &c) || !ReadNumber(d, &i, &e)) { i = d.size(); break; }
            if (cmd == 's') { a += cx; b += cy; c += cx; e += cy; }
            AddSegment(g, cx, cy, c, e, a, b, a, b);
            cx = c; cy = e;
            break;
        }
        case 'Q': case 'q': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b) ||
                !ReadNumber(d, &i, &c) || !ReadNumber(d, &i, &e)) { i = d.size(); break; }
            if (cmd == 'q') { a += cx; b += cy; c += cx; e += cy; }
            AddSegment(g, cx, cy, c, e, a, b, a, b);
            cx = c; cy = e;
            break;
        }
        case 'T': case 't': {
            if (!ReadNumber(d, &i, &a) || !ReadNumber(d, &i, &b)) { i = d.size(); break; }
            if (cmd == 't') { a += cx; b += cy; }
            AddSegment(g, cx, cy, a, b, a, b, cx, cy);
            cx = a; cy = b;
            break;
        }
        case 'A': case 'a': {
            // rx ry rotation large-arc sweep x y
            float v[7];
            bool ok = true;
            for (int k = 0; k < 7; ++k) {
                if (!ReadNumber(d, &i, &v[k])) { ok = false; break; }
            }
            if (!ok) { i = d.size(); break; }
            float x = v[5], y = v[6];
            if (cmd == 'a') { x += cx; y += cy; }
            AddSegment(g, cx, cy, x, y, x, y, cx, cy);
            cx = x; cy = y;
            break;
        }
        case 'Z': case 'z': {
            AddSegment(g, cx, cy, subX, subY, subX, subY, cx, cy);
            cx = subX; cy = subY;
            break;
        }
        default:
            // Unknown command: advance one character so the loop terminates.
            ++i;
            break;
        }
    }
}

// Same for a polyline/polygon "points" list: every consecutive pair is a
// segment, so the first pair gives the start tangent and the last the end.
void ParsePointsGeometry(const std::string& pts, PathGeom* g) {
    std::vector<float> v;
    size_t i = 0;
    float n = 0.0f;
    while (ReadNumber(pts, &i, &n)) v.push_back(n);
    for (size_t k = 2; k + 1 < v.size(); k += 2) {
        AddSegment(g, v[k - 2], v[k - 1], v[k], v[k + 1],
                   v[k], v[k + 1], v[k - 2], v[k - 1]);
    }
}

void CollectMarkers(const std::string& xml, std::vector<MarkerDef>* out) {
    size_t p = 0;
    while ((p = xml.find("<marker", p)) != std::string::npos) {
        const size_t gt = xml.find('>', p);
        if (gt == std::string::npos) break;
        const std::string tag = xml.substr(p, gt - p + 1);
        if (IsSelfClosed(tag)) { p = gt + 1; continue; }
        const size_t close = xml.find("</marker>", gt);
        if (close == std::string::npos) break;
        MarkerDef m;
        m.id = GetAttr(tag, "id");
        std::string v;
        v = GetAttr(tag, "markerWidth");
        if (!v.empty()) m.markerW = ToFloat(v, 3.0f);
        v = GetAttr(tag, "markerHeight");
        if (!v.empty()) m.markerH = ToFloat(v, 3.0f);
        v = GetAttr(tag, "refX");
        if (!v.empty()) m.refX = ToFloat(v, 0.0f);
        v = GetAttr(tag, "refY");
        if (!v.empty()) m.refY = ToFloat(v, 0.0f);
        v = GetAttr(tag, "markerUnits");
        if (!v.empty()) m.strokeUnits = (v != "userSpaceOnUse");
        m.orient = GetAttr(tag, "orient");
        m.transform = GetAttr(tag, "transform");
        v = GetAttr(tag, "viewBox");
        if (v.empty()) v = GetAttr(tag, "viewbox");
        if (!v.empty()) {
            float vb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            size_t k = 0, q = 0;
            while (k < 4 && ReadNumber(v, &q, &vb[k])) ++k;
            if (k == 4 && vb[2] > 0.0f && vb[3] > 0.0f) {
                m.hasViewBox = true;
                m.vbX = vb[0];
                m.vbY = vb[1];
                m.vbW = vb[2];
                m.vbH = vb[3];
            }
        }
        m.content = xml.substr(gt + 1, close - gt - 1);
        if (!m.id.empty()) out->push_back(m);
        p = close + 9;
    }
}

// Removes name="..." (or name='...') and the whitespace before it.
std::string StripAttr(const std::string& tag, const std::string& name) {
    std::string out = tag;
    const char quotes[2] = {'"', '\''};
    for (int qi = 0; qi < 2; ++qi) {
        const std::string pat = name + "=" + std::string(1, quotes[qi]);
        size_t p = out.find(pat);
        while (p != std::string::npos) {
            if (p > 0 && (std::isalnum(static_cast<unsigned char>(out[p - 1])) ||
                          out[p - 1] == '-' || out[p - 1] == '_' ||
                          out[p - 1] == ':')) {
                p = out.find(pat, p + pat.size());
                continue;
            }
            const size_t end = out.find(quotes[qi], p + pat.size());
            if (end == std::string::npos) break;
            size_t b = p;
            while (b > 0 && (out[b - 1] == ' ' || out[b - 1] == '\t' ||
                             out[b - 1] == '\n' || out[b - 1] == '\r')) --b;
            out.erase(b, end + 1 - b);
            p = out.find(pat, b);
        }
    }
    return out;
}

// "url(#arrow)" -> "arrow".
std::string MarkerRefId(const std::string& v) {
    const size_t p = v.find('#');
    if (p == std::string::npos) return {};
    size_t e = v.find(')', p);
    if (e == std::string::npos) e = v.size();
    return v.substr(p + 1, e - p - 1);
}

void EmitMarker(std::string& out, const std::vector<MarkerDef>& defs,
                const std::string& id, bool have, float x, float y,
                float dx, float dy, bool isStart, const std::string& elemTf,
                float strokeWidth) {
    if (!have || id.empty()) return;
    const MarkerDef* m = nullptr;
    for (const auto& d : defs) {
        if (d.id == id) { m = &d; break; }
    }
    if (!m || m->content.empty()) return;
    if (dx == 0.0f && dy == 0.0f) return;

    // orient: "auto" follows the path, "auto-start-reverse" turns a start
    // marker around, a bare number is a fixed angle, and the default is 0.
    float ang = 0.0f;
    if (m->orient == "auto" || m->orient == "auto-start-reverse") {
        ang = std::atan2(dy, dx) * 180.0f / 3.14159265358979f;
        if (isStart && m->orient == "auto-start-reverse") ang += 180.0f;
    } else if (!m->orient.empty()) {
        ang = ToFloat(m->orient, 0.0f);
    }

    const float s = m->strokeUnits ? (strokeWidth > 0.0f ? strokeWidth : 1.0f)
                                   : 1.0f;

    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "<g transform=\"%s translate(%.4f,%.4f) rotate(%.4f) "
                  "scale(%.6f) translate(%.4f,%.4f)",
                  elemTf.c_str(), x, y, ang, s, -m->refX, -m->refY);
    out += buf;
    if (m->hasViewBox) {
        // The content is fitted into markerWidth x markerHeight with the
        // default preserveAspectRatio (xMidYMid meet).
        const float sx = m->markerW / m->vbW;
        const float sy = m->markerH / m->vbH;
        const float vs = (sx < sy) ? sx : sy;
        const float tx = (m->markerW - m->vbW * vs) * 0.5f;
        const float ty = (m->markerH - m->vbH * vs) * 0.5f;
        std::snprintf(buf, sizeof buf,
                      " translate(%.4f,%.4f) scale(%.6f) translate(%.4f,%.4f)",
                      tx, ty, vs, -m->vbX, -m->vbY);
        out += buf;
    }
    if (!m->transform.empty()) {
        out += " ";
        out += m->transform;
    }
    out += "\">";
    out += m->content;
    out += "</g>";
}

}  // namespace

std::string ExpandMarkers(const std::string& xml) {
    if (xml.find("marker-end") == std::string::npos &&
        xml.find("marker-start") == std::string::npos) {
        return xml;
    }
    std::vector<MarkerDef> defs;
    CollectMarkers(xml, &defs);
    // No early return when defs is empty: a reference to a marker that is not
    // defined still has to lose its marker-* attribute, because Direct2D does
    // not understand it. EmitMarker draws nothing for an unknown id.

    std::string out;
    out.reserve(xml.size() + xml.size() / 8);
    size_t pos = 0;
    while (pos < xml.size()) {
        const size_t lt = xml.find('<', pos);
        if (lt == std::string::npos) {
            out += xml.substr(pos);
            break;
        }
        out += xml.substr(pos, lt - pos);
        // Comments, CDATA and processing instructions are copied whole: a
        // '>' inside them does not end a tag.
        if (xml.compare(lt, 4, "<!--") == 0) {
            const size_t e = xml.find("-->", lt + 4);
            if (e == std::string::npos) { out += xml.substr(lt); break; }
            out += xml.substr(lt, e + 3 - lt);
            pos = e + 3;
            continue;
        }
        if (xml.compare(lt, 9, "<![CDATA[") == 0) {
            const size_t e = xml.find("]]>", lt + 9);
            if (e == std::string::npos) { out += xml.substr(lt); break; }
            out += xml.substr(lt, e + 3 - lt);
            pos = e + 3;
            continue;
        }
        if (lt + 1 < xml.size() && (xml[lt + 1] == '?' || xml[lt + 1] == '!')) {
            const size_t e = xml.find('>', lt + 2);
            if (e == std::string::npos) { out += xml.substr(lt); break; }
            out += xml.substr(lt, e + 1 - lt);
            pos = e + 1;
            continue;
        }
        const size_t gt = xml.find('>', lt + 1);
        if (gt == std::string::npos) { out += xml.substr(lt); break; }
        const std::string tag = xml.substr(lt, gt - lt + 1);
        pos = gt + 1;

        const bool isClose = tag.size() > 1 && tag[1] == '/';
        const std::string me = isClose ? std::string() : GetAttr(tag, "marker-end");
        const std::string ms = isClose ? std::string() : GetAttr(tag, "marker-start");
        if (me.empty() && ms.empty()) {
            out += tag;
            continue;
        }

        // The marker is placed in the element's own user space, so the
        // element's transform has to lead the group's transform list.
        const std::string elemTf = GetAttr(tag, "transform");
        float strokeW = 1.0f;
        {
            const std::string v = GetAttr(tag, "stroke-width");
            if (!v.empty()) strokeW = ToFloat(v, 1.0f);
        }
        PathGeom g;
        if (TagIs(tag, "path")) {
            ParsePathGeometry(GetAttr(tag, "d"), &g);
        } else if (TagIs(tag, "line")) {
            const float x1 = ToFloat(GetAttr(tag, "x1"), 0.0f);
            const float y1 = ToFloat(GetAttr(tag, "y1"), 0.0f);
            const float x2 = ToFloat(GetAttr(tag, "x2"), 0.0f);
            const float y2 = ToFloat(GetAttr(tag, "y2"), 0.0f);
            AddSegment(&g, x1, y1, x2, y2, x2, y2, x1, y1);
        } else if (TagIs(tag, "polyline") || TagIs(tag, "polygon")) {
            ParsePointsGeometry(GetAttr(tag, "points"), &g);
        }

        out += StripAttr(StripAttr(tag, "marker-end"), "marker-start");
        if (!me.empty()) {
            EmitMarker(out, defs, MarkerRefId(me), g.hasEnd, g.ex, g.ey,
                       g.edx, g.edy, false, elemTf, strokeW);
        }
        if (!ms.empty()) {
            EmitMarker(out, defs, MarkerRefId(ms), g.hasStartDir, g.sx, g.sy,
                       g.sdx, g.sdy, true, elemTf, strokeW);
        }
    }
    return out;
}

// Reads the root <svg> width/height, falling back to the viewBox extent.
// A percentage width is not a size (it depends on the viewport), so it is
// ignored in favour of the viewBox, which is what a browser does.
bool ReadSize(const std::string& xml, float* w, float* h) {
    if (w) *w = 0.0f;
    if (h) *h = 0.0f;
    const size_t p = xml.find("<svg");
    if (p == std::string::npos) return false;
    const size_t gt = xml.find('>', p);
    if (gt == std::string::npos) return false;
    const std::string tag = xml.substr(p, gt - p + 1);

    float rw = 0.0f, rh = 0.0f;
    const std::string ws = GetAttr(tag, "width");
    const std::string hs = GetAttr(tag, "height");
    if (ws.find('%') == std::string::npos) rw = ToFloat(ws, 0.0f);
    if (hs.find('%') == std::string::npos) rh = ToFloat(hs, 0.0f);

    if (!(rw > 0.0f) || !(rh > 0.0f)) {
        // viewBox is case-sensitive per spec, but real files vary (Batik
        // writes lowercase "viewbox"); try both spellings.
        std::string vb = GetAttr(tag, "viewBox");
        if (vb.empty()) vb = GetAttr(tag, "viewbox");
        if (!vb.empty()) {
            float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            size_t k = 0, q = 0;
            while (k < 4 && ReadNumber(vb, &q, &v[k])) ++k;
            if (k == 4) {
                rw = v[2];
                rh = v[3];
            }
        }
    }
    if (!(rw > 0.0f) || !(rh > 0.0f)) return false;
    if (w) *w = rw;
    if (h) *h = rh;
    return true;
}

}  // namespace svg
