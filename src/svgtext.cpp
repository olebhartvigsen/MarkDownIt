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

    int defsDepth = 0;

    // Stack of parent text attributes for <tspan> inheritance.
    struct TextCtx {
        float x = 0, y = 0, fontSize = 16;
        std::string fontFamily;
        std::string anchor;
        std::string fill;
        bool bold = false;
    };
    std::vector<TextCtx> stack;

    while (true) {
        std::string tag = p.NextTag();
        if (tag.empty()) break;

        if (TagIs(tag, "defs")) {
            if (tag.size() > 1 && tag[1] != '/') defsDepth++;
            else defsDepth = (defsDepth > 0) ? defsDepth - 1 : 0;
            continue;
        }

        // Skip everything inside <defs>
        if (defsDepth > 0) continue;

        // Opening <text ...>
        if (TagIs(tag, "text") && tag.size() > 1 && tag[1] != '/') {
            TextCtx ctx;
            ctx.x = ToFloat(GetAttrOrStyle(tag, "x"));
            ctx.y = ToFloat(GetAttrOrStyle(tag, "y"));
            ctx.fontSize = ToFloat(GetAttrOrStyle(tag, "font-size"), 16.0f);
            ctx.fontFamily = GetAttrOrStyle(tag, "font-family");
            ctx.anchor = GetAttrOrStyle(tag, "text-anchor");
            ctx.fill = GetAttrOrStyle(tag, "fill");
            std::string fw = GetAttrOrStyle(tag, "font-weight");
            ctx.bold = (fw == "bold" || fw == "700" || fw == "bolder");

            stack.push_back(ctx);

            // Collect text content, handling tspan children
            // Loop until matching </text>
            while (!stack.empty()) {
                std::string text = p.TextUntilTag();
                // Trim whitespace
                size_t a = text.find_first_not_of(" \t\n\r");
                size_t z = text.find_last_not_of(" \t\n\r");
                if (a != std::string::npos && z != std::string::npos && z >= a) {
                    std::string trimmed = text.substr(a, z - a + 1);
                    if (!trimmed.empty()) {
                        TextRun run;
                        run.x = stack.back().x;
                        run.y = stack.back().y;
                        run.fontSize = stack.back().fontSize;
                        run.fontFamily = stack.back().fontFamily;
                        run.anchor = stack.back().anchor;
                        run.fill = stack.back().fill;
                        run.bold = stack.back().bold;
                        run.text = DecodeEntities(trimmed);
                        runs.push_back(run);
                    }
                }

                std::string inner = p.NextTag();
                if (inner.empty()) break;

                if (TagIs(inner, "/text")) {
                    stack.pop_back();
                    break;
                }
                if (TagIs(inner, "tspan") && inner.size() > 1 && inner[1] != '/') {
                    // Push inherited context with overrides
                    TextCtx child = stack.back();
                    std::string v;
                    v = GetAttrOrStyle(inner, "x"); if (!v.empty()) child.x = ToFloat(v);
                    v = GetAttrOrStyle(inner, "y"); if (!v.empty()) child.y = ToFloat(v);
                    v = GetAttrOrStyle(inner, "font-size"); if (!v.empty()) child.fontSize = ToFloat(v);
                    v = GetAttrOrStyle(inner, "font-family"); if (!v.empty()) child.fontFamily = v;
                    v = GetAttrOrStyle(inner, "text-anchor"); if (!v.empty()) child.anchor = v;
                    v = GetAttrOrStyle(inner, "fill"); if (!v.empty()) child.fill = v;
                    v = GetAttrOrStyle(inner, "font-weight");
                    if (!v.empty()) child.bold = (v == "bold" || v == "700" || v == "bolder");
                    stack.push_back(child);
                    // Self-closing tspan?
                    if (!inner.empty() && inner.back() == '/') {
                        stack.pop_back();
                    }
                    continue;
                }
                if (TagIs(inner, "/tspan")) {
                    if (stack.size() > 1) stack.pop_back();
                    continue;
                }
                // Any other tag inside text: skip
                // If it's an opening tag we don't recognize, look for its close
            }
            continue;
        }

        // Skip everything else
        // For opening tags we don't care about, just continue.
        // For self-closing tags, just continue.
        // For closing tags, just continue.
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
