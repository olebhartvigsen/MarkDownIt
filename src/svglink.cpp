// Links inside an SVG diagram: the scan and the hit test.
//
// The rules the parse follows are the ones a diagram export actually
// writes, not the whole of the SVG spec: an <a> wrapping the shapes of
// one node, with the URL in href or xlink:href. Geometry comes from the
// child elements the readers already understand, so a link's clickable
// box is the visible box of its shape.

#include "svglink.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace svg {
namespace {

struct Box {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    bool valid = false;

    void Add(float x, float y) {
        if (!valid) {
            x0 = x1 = x;
            y0 = y1 = y;
            valid = true;
            return;
        }
        x0 = (std::min)(x0, x);
        y0 = (std::min)(y0, y);
        x1 = (std::max)(x1, x);
        y1 = (std::max)(y1, y);
    }
    void AddRect(float x, float y, float w, float h) {
        Add(x, y);
        Add(x + w, y + h);
    }
};

std::string DecodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const size_t semi = text.find(';', i);
        if (semi == std::string::npos || semi - i > 8) {
            out.push_back(text[i]);
            continue;
        }
        const std::string name = text.substr(i + 1, semi - i - 1);
        if (name == "amp") out.push_back('&');
        else if (name == "lt") out.push_back('<');
        else if (name == "gt") out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else if (!name.empty() && name[0] == '#') {
            const long code = std::strtol(name.c_str() + 1, nullptr, 10);
            if (code > 0 && code < 128) {
                out.push_back(static_cast<char>(code));
            }
        } else {
            out.append(text, i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

// The value of an attribute in a tag, without its quotes.
bool Attribute(const std::string& tag, const char* name, std::string* out) {
    const size_t n = std::strlen(name);
    size_t at = 0;
    while ((at = tag.find(name, at)) != std::string::npos) {
        // The name must be a whole attribute: not part of a longer one.
        const bool left_ok = at == 0 || std::isspace(
            static_cast<unsigned char>(tag[at - 1])) || tag[at - 1] == '<';
        size_t after = at + n;
        while (after < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[after]))) {
            ++after;
        }
        if (left_ok && after < tag.size() && tag[after] == '=') {
            ++after;
            while (after < tag.size() &&
                   std::isspace(static_cast<unsigned char>(tag[after]))) {
                ++after;
            }
            if (after >= tag.size()) return false;
            const char quote = tag[after];
            if (quote == '"' || quote == '\'') {
                const size_t close = tag.find(quote, after + 1);
                if (close == std::string::npos) return false;
                *out = DecodeEntities(tag.substr(after + 1, close - after - 1));
                return true;
            }
            const size_t end = tag.find_first_of(" \t\r\n/>", after);
            *out = DecodeEntities(tag.substr(
                after, end == std::string::npos ? std::string::npos
                                                : end - after));
            return true;
        }
        at += n;
    }
    return false;
}

float Number(const std::string& tag, const char* name, float fallback) {
    std::string value;
    if (!Attribute(tag, name, &value) || value.empty()) return fallback;
    char* stop = nullptr;
    const double v = std::strtod(value.c_str(), &stop);
    if (stop == value.c_str()) return fallback;
    return static_cast<float>(v);
}

// Every number in a points/ path string, in order.
std::vector<float> Numbers(const std::string& text) {
    std::vector<float> out;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        const bool starts = (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                            c == '.';
        if (!starts) {
            ++i;
            continue;
        }
        char* stop = nullptr;
        const double v = std::strtod(text.c_str() + i, &stop);
        if (!stop || stop == text.c_str() + i) {
            ++i;
            continue;
        }
        out.push_back(static_cast<float>(v));
        i = static_cast<size_t>(stop - text.c_str());
    }
    return out;
}

// The box a single child element occupies.
void BoxOfElement(const std::string& tag, Box* box) {
    const auto has = [&tag](const char* word) {
        return tag.compare(0, std::strlen(word), word) == 0;
    };
    if (has("<rect")) {
        const float x = Number(tag, "x", 0.0f);
        const float y = Number(tag, "y", 0.0f);
        box->AddRect(x, y, Number(tag, "width", 0.0f),
                     Number(tag, "height", 0.0f));
        return;
    }
    if (has("<circle")) {
        const float cx = Number(tag, "cx", 0.0f);
        const float cy = Number(tag, "cy", 0.0f);
        const float r = Number(tag, "r", 0.0f);
        box->AddRect(cx - r, cy - r, 2.0f * r, 2.0f * r);
        return;
    }
    if (has("<ellipse")) {
        const float cx = Number(tag, "cx", 0.0f);
        const float cy = Number(tag, "cy", 0.0f);
        const float rx = Number(tag, "rx", 0.0f);
        const float ry = Number(tag, "ry", 0.0f);
        box->AddRect(cx - rx, cy - ry, 2.0f * rx, 2.0f * ry);
        return;
    }
    if (has("<line")) {
        box->Add(Number(tag, "x1", 0.0f), Number(tag, "y1", 0.0f));
        box->Add(Number(tag, "x2", 0.0f), Number(tag, "y2", 0.0f));
        return;
    }
    if (has("<polygon") || has("<polyline")) {
        std::string points;
        if (!Attribute(tag, "points", &points)) return;
        const std::vector<float> v = Numbers(points);
        for (size_t i = 0; i + 1 < v.size(); i += 2) box->Add(v[i], v[i + 1]);
        return;
    }
    if (has("<path")) {
        // Control points count: a path's box is allowed to be a little
        // generous, because it is a click target and not a boundary.
        std::string d;
        if (!Attribute(tag, "d", &d)) return;
        const std::vector<float> v = Numbers(d);
        for (size_t i = 0; i + 1 < v.size(); i += 2) box->Add(v[i], v[i + 1]);
        return;
    }
    if (has("<text")) {
        const float x = Number(tag, "x", 0.0f);
        const float y = Number(tag, "y", 0.0f);
        const float size = Number(tag, "font-size", 12.0f);
        // The words are in the element's body, not the tag; a rough
        // width keeps a label clickable even when it is the only child.
        const float width = size * 6.0f;
        box->AddRect(x, y - size, width, size * 1.4f);
        return;
    }
}

}  // namespace

std::vector<LinkBox> CollectLinks(const std::string& xml) {
    std::vector<LinkBox> links;
    size_t at = 0;
    while ((at = xml.find("<a", at)) != std::string::npos) {
        const size_t open_end = xml.find('>', at);
        if (open_end == std::string::npos) break;
        // "<a" must be the anchor element, not <animate> or <adapter>.
        const char after = at + 2 < xml.size() ? xml[at + 2] : '\0';
        if (!(after == '>' || std::isspace(static_cast<unsigned char>(after)) ||
              after == '/')) {
            at += 2;
            continue;
        }
        const std::string open_tag = xml.substr(at, open_end - at + 1);
        std::string href;
        if (!Attribute(open_tag, "href", &href) || href.empty()) {
            if (!Attribute(open_tag, "xlink:href", &href) || href.empty()) {
                at = open_end + 1;
                continue;
            }
        }
        const size_t close = xml.find("</a>", open_end);
        if (close == std::string::npos) break;
        const std::string body = xml.substr(open_end + 1, close - open_end - 1);

        Box box;
        size_t child = 0;
        while ((child = body.find('<', child)) != std::string::npos) {
            if (child + 1 >= body.size()) break;
            const char next = body[child + 1];
            if (next == '/' || next == '!' || next == '?') {
                child += 2;
                continue;
            }
            const size_t child_end = body.find('>', child);
            if (child_end == std::string::npos) break;
            BoxOfElement(body.substr(child, child_end - child + 1), &box);
            child = child_end + 1;
        }
        if (box.valid && box.x1 > box.x0 && box.y1 > box.y0) {
            LinkBox link;
            link.x = box.x0;
            link.y = box.y0;
            link.width = box.x1 - box.x0;
            link.height = box.y1 - box.y0;
            link.href = href;
            links.push_back(link);
        }
        at = close + 4;
    }
    return links;
}

const LinkBox* LinkAt(const std::vector<LinkBox>& links, float u, float v) {
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const LinkBox& b = *it;
        if (u >= b.x && u <= b.x + b.width && v >= b.y &&
            v <= b.y + b.height) {
            return &b;
        }
    }
    return nullptr;
}

bool IsOpenableUrl(const std::string& href) {
    std::string scheme;
    for (size_t i = 0; i < href.size(); ++i) {
        const char c = href[i];
        if (c == ':') break;
        if (!std::isalpha(static_cast<unsigned char>(c)) &&
            !(i > 0 && (std::isdigit(static_cast<unsigned char>(c)) ||
                        c == '+' || c == '-' || c == '.'))) {
            return false;
        }
        scheme.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    // Only encrypted web links and mail. A diagram can be written by
    // anyone, and a plain-http target is one a reader cannot tell apart
    // from a secure one once the browser has it.
    return scheme == "https" || scheme == "mailto";
}

}  // namespace svg
