#include "svgtext.h"

#include <algorithm>
#include <cstdlib>

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
    std::string pattern = name + "=\"";
    size_t p = tag.find(pattern);
    if (p == std::string::npos) {
        pattern = name + "='";
        p = tag.find(pattern);
    }
    if (p == std::string::npos) return {};
    p += pattern.size();
    char quote = tag[p - 1];
    size_t end = tag.find(quote, p);
    if (end == std::string::npos) return {};
    return tag.substr(p, end - p);
}

static float ToFloat(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    try {
        return std::stof(s);
    } catch (...) {
        return def;
    }
}

// Parse style attribute for font-size, font-family, etc.
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

// Get an attribute, falling back to the style attribute.
static std::string GetAttrOrStyle(const std::string& tag, const std::string& name) {
    std::string v = GetAttr(tag, name);
    if (!v.empty()) return v;
    return GetStyleValue(tag, name);
}

// Check if the tag name matches (case-insensitive).
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
    };
    std::vector<Ctx> gStack;
    Ctx root;
    gStack.push_back(root);

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
            size_t nameStart = pos;
            while (pos < tf.size() && tf[pos] >= 97 && tf[pos] <= 122) pos++;
            if (pos == nameStart) { pos++; continue; }
            std::string name = tf.substr(nameStart, pos - nameStart);
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
            } else { continue; }
            float temp[6];
            ComposeOp(temp, out, op);
            for (int mi = 0; mi < 6; mi++) out[mi] = temp[mi];
        }
    };

    int defsDepth = 0;

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
        if (defsDepth > 0) continue;

        // <g> open/close: transform + inheritable style props.
        if (TagIs(tag, "g") && tag.size() > 1 && tag[1] != 47) {
            Ctx child = gStack.back();
            std::string tf = GetAttrOrStyle(tag, "transform");
            if (!tf.empty() && tf != "none") {
                float composed[6];
                ParseTransform(tf, composed);
                float out2[6];
                ComposeOp(out2, child.m, composed);
                for (int mi = 0; mi < 6; mi++) child.m[mi] = out2[mi];
            }
            std::string v;
            v = GetAttrOrStyle(tag, "font-size");
            if (!v.empty()) {
                // ToFloat stops at the px/pt suffix; scale by the
                // freshly composed vertical scale factor.
                child.fontSize = ToFloat(v, child.fontSize) * child.m[3];
            }
            v = GetAttrOrStyle(tag, "font-family");
            if (!v.empty()) child.fontFamily = v;
            v = GetAttrOrStyle(tag, "text-anchor");
            if (!v.empty()) child.anchor = v;
            v = GetAttrOrStyle(tag, "fill");
            if (!v.empty() && v != "none") child.fill = v;
            v = GetAttrOrStyle(tag, "font-weight");
            if (!v.empty()) child.bold = (v == "bold" || v == "700" || v == "bolder");
            gStack.push_back(child);
            // self-closing g?
            if (!tag.empty() && tag.back() == 47) gStack.pop_back();
            continue;
        }
        if (TagIs(tag, "/g")) {
            if (gStack.size() > 1) gStack.pop_back();
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
            std::string v;
            v = GetAttrOrStyle(tag, "font-size");
            if (!v.empty()) ctx.fontSize = ToFloat(v, ctx.fontSize) * ctx.m[3];
            v = GetAttrOrStyle(tag, "font-family");
            if (!v.empty()) ctx.fontFamily = v;
            v = GetAttrOrStyle(tag, "text-anchor");
            if (!v.empty()) ctx.anchor = v;
            v = GetAttrOrStyle(tag, "fill");
            if (!v.empty() && v != "none") ctx.fill = v;
            v = GetAttrOrStyle(tag, "font-weight");
            if (!v.empty()) ctx.bold = (v == "bold" || v == "700" || v == "bolder");
            float xBase = firstNum(GetAttrOrStyle(tag, "x"), 0.0f);
            float yBase = firstNum(GetAttrOrStyle(tag, "y"), 0.0f);

            while (true) {
                std::string text = p.TextUntilTag();
                size_t a = text.find_first_not_of(" \t\n\r");
                size_t z = text.find_last_not_of(" \t\n\r");
                if (a != std::string::npos && z != std::string::npos && z >= a) {
                    std::string trimmed = text.substr(a, z - a + 1);
                    if (!trimmed.empty()) {
                        // Transform position through composed ctx.m.
                        Ctx use = gStack.back();
                        // tspan overrides currently on stack top.
                        TextRun run;
                        float px = xBase, py = yBase;
                        float lx = use.m[0] * px + use.m[2] * py + use.m[4];
                        float ly = use.m[1] * px + use.m[3] * py + use.m[5];
                        run.x = lx;
                        run.y = ly;
                        run.fontSize = use.fontSize;
                        run.fontFamily = use.fontFamily;
                        run.anchor = use.anchor;
                        run.fill = use.fill;
                        run.bold = use.bold;
                        run.text = DecodeEntities(trimmed);
                        runs.push_back(run);
                    }
                }
                std::string inner = p.NextTag();
                if (inner.empty()) break;
                if (TagIs(inner, "/text")) break;
                if (TagIs(inner, "tspan") && inner.size() > 1 && inner[1] != 47) {
                    Ctx child = gStack.back();
                    std::string v2;
                    v2 = GetAttrOrStyle(inner, "x");
                    if (!v2.empty()) xBase = firstNum(v2, xBase);
                    v2 = GetAttrOrStyle(inner, "y");
                    std::string yv = GetAttrOrStyle(inner, "y");
                    if (!yv.empty()) yBase = firstNum(yv, yBase);
                    v2 = GetAttrOrStyle(inner, "font-size");
                    if (!v2.empty()) child.fontSize = ToFloat(v2, child.fontSize) * child.m[3];
                    v2 = GetAttrOrStyle(inner, "font-family");
                    if (!v2.empty()) child.fontFamily = v2;
                    v2 = GetAttrOrStyle(inner, "text-anchor");
                    if (!v2.empty()) child.anchor = v2;
                    v2 = GetAttrOrStyle(inner, "fill");
                    if (!v2.empty() && v2 != "none") child.fill = v2;
                    v2 = GetAttrOrStyle(inner, "font-weight");
                    if (!v2.empty()) child.bold = (v2 == "bold" || v2 == "700" || v2 == "bolder");
                    gStack.push_back(child);
                    if (!inner.empty() && inner.back() == 47) gStack.pop_back();
                    continue;
                }
                if (TagIs(inner, "/tspan")) {
                    if (gStack.size() > 1) gStack.pop_back();
                    continue;
                }
            }
            continue;
        }
    }

    return runs;
}

// --- StripTextElements ---

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
            if (isOpen && isText && tag.back() != '/') stripDepth++;
            else if (!isOpen) {
                // Closing tag
                if (isText || TagIs(tag, "/text") || TagIs(tag, "/tspan")
                    || TagIs(tag, "/foreignObject")) {
                    stripDepth--;
                }
            }
        } else {
            if (isOpen && isText) {
                if (tag.back() == '/') {
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

}  // namespace svg
