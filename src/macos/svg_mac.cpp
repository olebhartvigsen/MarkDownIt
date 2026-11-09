// An SVG document for the macOS renderer.
//
// The elements are read with the XML parser the office bridge already
// uses, and the shapes are drawn with the render target, so the SVG
// goes through the same narrow interface as the rest of the document.

#include "svg_mac.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "pugixml.hpp"
#include "svgtext.h"

namespace platform {
namespace {

// The first family of a CSS font-family list, trimmed. Core Text takes one
// family name, and an SVG root usually carries a list ("Arial, Helvetica,
// sans-serif"). Passing the list through finds no such family and silently
// falls back, which is how every label ended up in the wrong face.
std::string FirstFamily(const std::string& list) {
    std::string out;
    for (char c : list) {
        if (c == ',') break;
        if (c != '\'' && c != '"') out.push_back(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    size_t b = 0;
    while (b < out.size() && (out[b] == ' ' || out[b] == '\t')) ++b;
    out = out.substr(b);
    // The generic CSS families have no Core Text family of their own.
    if (out == "sans-serif") return "Helvetica";
    if (out == "serif") return "Times New Roman";
    if (out == "monospace") return "Menlo";
    return out;
}

// True when a style attribute asks for an italic face. The attribute form is
// the common one, but exporters also write style="font-style:italic".
bool StyleSaysItalic(const char* style) {
    if (!style) return false;
    const std::string text(style);
    const size_t p = text.find("font-style");
    if (p == std::string::npos) return false;
    const size_t colon = text.find(':', p);
    if (colon == std::string::npos) return false;
    const size_t end = text.find(';', colon);
    std::string value = text.substr(colon + 1,
                                    end == std::string::npos
                                        ? std::string::npos
                                        : end - colon - 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value == "italic" || value == "oblique";
}

// Reads a number out of an attribute, and says whether it was there.
bool ReadNumber(const pugi::xml_attribute& attribute, float* out) {
    if (!attribute) return false;
    const char* text = attribute.value();
    if (!text || !*text) return false;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) return false;
    *out = static_cast<float>(value);
    return true;
}

// Reads a length, which may carry a unit such as "12px" or "50%".
//
// The unit is dropped and the number is used, which is right for the
// absolute units and close enough for the others.
float ReadLength(const pugi::xml_attribute& attribute, float fallback) {
    float value = fallback;
    if (!ReadNumber(attribute, &value)) return fallback;
    return value;
}

// Reads a colour out of an attribute.
//
// The colours the documents use are the named ones and the ones with
// a number in them, so both are read here.
bool ReadColor(const std::string& text, Color* out) {
    if (text.empty()) return false;
    if (text == "none" || text == "transparent") return false;

    if (text[0] == '#') {
        const std::string hex = text.substr(1);
        if (hex.size() == 3 || hex.size() == 4) {
            auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out->r = digit(hex[0]) * 17 / 255.0f;
            out->g = digit(hex[1]) * 17 / 255.0f;
            out->b = digit(hex[2]) * 17 / 255.0f;
            out->a = hex.size() == 4 ? digit(hex[3]) * 17 / 255.0f : 1.0f;
            return true;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out->r = (digit(hex[0]) * 16 + digit(hex[1])) / 255.0f;
            out->g = (digit(hex[2]) * 16 + digit(hex[3])) / 255.0f;
            out->b = (digit(hex[4]) * 16 + digit(hex[5])) / 255.0f;
            out->a = hex.size() == 8
                          ? (digit(hex[6]) * 16 + digit(hex[7])) / 255.0f
                          : 1.0f;
            return true;
        }
        return false;
    }

    if (text.compare(0, 4, "rgb(") == 0) {
        const size_t open = text.find('(');
        const size_t close = text.find(')');
        if (open == std::string::npos || close == std::string::npos) return false;
        const std::string inner = text.substr(open + 1, close - open - 1);
        float parts[3] = {0.0f, 0.0f, 0.0f};
        size_t start = 0;
        for (int i = 0; i < 3; ++i) {
            const size_t comma = inner.find(',', start);
            const std::string piece = inner.substr(
                start, comma == std::string::npos ? std::string::npos
                                                 : comma - start);
            parts[i] = static_cast<float>(std::atof(piece.c_str()));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        out->r = parts[0] / 255.0f;
        out->g = parts[1] / 255.0f;
        out->b = parts[2] / 255.0f;
        out->a = 1.0f;
        return true;
    }

    // The named colours the documents use most.
    struct Named {
        const char* name;
        float r;
        float g;
        float b;
    };
    static const Named kNamed[] = {
        {"black", 0.0f, 0.0f, 0.0f},
        {"white", 1.0f, 1.0f, 1.0f},
        {"red", 1.0f, 0.0f, 0.0f},
        {"green", 0.0f, 0.5f, 0.0f},
        {"blue", 0.0f, 0.0f, 1.0f},
        {"yellow", 1.0f, 1.0f, 0.0f},
        {"gray", 0.5f, 0.5f, 0.5f},
        {"grey", 0.5f, 0.5f, 0.5f},
        {"orange", 1.0f, 0.65f, 0.0f},
        {"purple", 0.5f, 0.0f, 0.5f},
        {"silver", 0.75f, 0.75f, 0.75f},
        {"navy", 0.0f, 0.0f, 0.5f},
        {"teal", 0.0f, 0.5f, 0.5f},
        {"olive", 0.5f, 0.5f, 0.0f},
        {"maroon", 0.5f, 0.0f, 0.0f},
        {"lime", 0.0f, 1.0f, 0.0f},
        {"aqua", 0.0f, 1.0f, 1.0f},
        {"cyan", 0.0f, 1.0f, 1.0f},
        {"magenta", 1.0f, 0.0f, 1.0f},
        {"fuchsia", 1.0f, 0.0f, 1.0f},
    };
    for (const Named& named : kNamed) {
        if (text == named.name) {
            out->r = named.r;
            out->g = named.g;
            out->b = named.b;
            out->a = 1.0f;
            return true;
        }
    }
    return false;
}

// Multiplies two transforms. The left one is applied after the right one,
// which is how a transform list reads: "translate(a) rotate(b)" rotates
// first and then translates.
SvgDoc::Placement Multiply(const SvgDoc::Placement& left,
                           const SvgDoc::Placement& right) {
    SvgDoc::Placement out;
    out.a = left.a * right.a + left.c * right.b;
    out.b = left.b * right.a + left.d * right.b;
    out.c = left.a * right.c + left.c * right.d;
    out.d = left.b * right.c + left.d * right.d;
    out.e = left.a * right.e + left.c * right.f + left.e;
    out.f = left.b * right.e + left.d * right.f + left.f;
    return out;
}

// The factor a transform applies to a stroke width. A marker carries a
// rotation and a uniform scale, where the two column lengths agree; the
// average keeps a width sane when a caller scales unevenly.
float MatrixScale(const SvgDoc::Placement& placement) {
    const float x = std::sqrt(placement.a * placement.a +
                              placement.b * placement.b);
    const float y = std::sqrt(placement.c * placement.c +
                              placement.d * placement.d);
    const float scale = (x + y) * 0.5f;
    return scale > 0.0f ? scale : 1.0f;
}

// Reads the placement of a group or a shape.
//
// Every function in the list becomes a matrix, and the list is composed in
// order. Markers are the reason this has to be complete: a marker is placed
// with translate, rotate, scale, translate, and leaving the rotation out made
// every arrowhead in a diagram point the same way.
SvgDoc::Placement ReadPlacement(const pugi::xml_node& node,
                                const SvgDoc::Placement& base) {
    SvgDoc::Placement placement = base;
    const char* transform = node.attribute("transform").value();
    if (!transform) return placement;

    const float kDegreesToRadians = 3.14159265358979f / 180.0f;
    const std::string text(transform);
    size_t at = 0;
    while (at < text.size()) {
        const size_t open = text.find('(', at);
        if (open == std::string::npos) break;
        const size_t close = text.find(')', open);
        if (close == std::string::npos) break;

        // The functions are separated by spaces or commas, so the name is
        // trimmed before it is compared.
        std::string name = text.substr(at, open - at);
        const char* kPad = " \t\r\n,";
        const size_t first = name.find_first_not_of(kPad);
        const size_t last = name.find_last_not_of(kPad);
        name = (first == std::string::npos) ? std::string()
                                            : name.substr(first, last - first + 1);

        const std::string inner = text.substr(open + 1, close - open - 1);
        std::vector<float> numbers;
        const char* p = inner.c_str();
        while (*p) {
            char* end = nullptr;
            const double value = std::strtod(p, &end);
            if (end == p) {
                ++p;
                continue;
            }
            numbers.push_back(static_cast<float>(value));
            p = end;
        }

        SvgDoc::Placement step;
        if (name == "translate" && !numbers.empty()) {
            step.e = numbers[0];
            step.f = numbers.size() > 1 ? numbers[1] : 0.0f;
        } else if (name == "scale" && !numbers.empty()) {
            step.a = numbers[0];
            step.d = numbers.size() > 1 ? numbers[1] : numbers[0];
        } else if (name == "rotate" && !numbers.empty()) {
            const float radians = numbers[0] * kDegreesToRadians;
            const float cs = std::cos(radians);
            const float sn = std::sin(radians);
            step.a = cs;
            step.b = sn;
            step.c = -sn;
            step.d = cs;
            if (numbers.size() >= 3) {
                // rotate(angle, cx, cy) turns around a point, which is a
                // translate before and after the turn.
                step.e = numbers[1] * (1.0f - cs) + sn * numbers[2];
                step.f = numbers[2] * (1.0f - cs) - sn * numbers[1];
            }
        } else if (name == "matrix" && numbers.size() >= 6) {
            step.a = numbers[0];
            step.b = numbers[1];
            step.c = numbers[2];
            step.d = numbers[3];
            step.e = numbers[4];
            step.f = numbers[5];
        } else if (name == "skewX" && !numbers.empty()) {
            step.c = std::tan(numbers[0] * kDegreesToRadians);
        } else if (name == "skewY" && !numbers.empty()) {
            step.b = std::tan(numbers[0] * kDegreesToRadians);
        }
        placement = Multiply(placement, step);
        at = close + 1;
    }
    return placement;
}

// A length written as text: "12px", "1.2em" and "12" all name a number,
// and the unit is dropped, which is what this parser works in.
float ReadLengthText(const std::string& text) {
    const char* p = text.c_str();
    while (*p && !std::isdigit(static_cast<unsigned char>(*p)) && *p != '-' &&
           *p != '.' && *p != '+') {
        ++p;
    }
    char* end = nullptr;
    const double value = std::strtod(p, &end);
    if (end == p) return 0.0f;
    return static_cast<float>(value);
}

// One declaration out of an inline style, trimmed; empty when absent.
std::string CssValue(const std::string& style, const std::string& property) {
    const std::string key = property + ":";
    size_t at = style.find(key);
    while (at != std::string::npos) {
        // A whole property name, not the tail of another one.
        if (at == 0 || style[at - 1] == ';' || style[at - 1] == ' ') {
            const size_t end = style.find(';', at);
            std::string value = style.substr(
                at + key.size(),
                (end == std::string::npos ? style.size() : end) - at - key.size());
            const size_t first = value.find_first_not_of(" \t\r\n");
            const size_t last = value.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string();
            return value.substr(first, last - first + 1);
        }
        at = style.find(key, at + 1);
    }
    return std::string();
}

// A CSS colour. light-dark() names the light value first, which is the one
// a light page shows; the first argument may itself hold commas inside
// rgb().
bool ReadCssColor(const std::string& value, Color* out) {
    const size_t at = value.find("light-dark");
    if (at != std::string::npos) {
        const size_t open = value.find('(', at);
        if (open == std::string::npos) return false;
        size_t depth = 0;
        size_t end = open + 1;
        for (; end < value.size(); ++end) {
            if (value[end] == '(') {
                ++depth;
            } else if (value[end] == ')') {
                if (depth == 0) break;
                --depth;
            } else if (value[end] == ',' && depth == 0) {
                break;
            }
        }
        return ReadColor(value.substr(open + 1, end - open - 1), out);
    }
    return ReadColor(value, out);
}

// The typography a browser draws for a diagram label.
//
// draw.io and its kin put every label inside a <switch> as HTML in a
// <foreignObject>, with the same words repeated as a <text> fallback for
// viewers without HTML. This renderer draws the fallback, because it has
// no HTML layout, but the typography of the HTML is what the browser
// shows: the exporter writes 'Architects Daughter' on the <text> while
// the label's own span says the family that actually renders.
struct HtmlLabel {
    bool valid = false;
    std::string fontFamily;
    float fontSize = 0.0f;
    bool hasColor = false;
    Color color;
    bool bold = false;
    bool italic = false;
    // The lines the browser lays the label out on, and the leading it uses
    // between them. A label is written as HTML in one place and as tspan
    // lines in the other, and the two disagree: a short label is often one
    // HTML line but two tspans, which drew an extra line.
    std::vector<std::string> lines;
    float lineHeight = 0.0f;   // in points, or a multiplier below 4
    bool centred = false;      // the HTML block is centred vertically
};

// The lines a browser lays the label's HTML out on: a block element and a
// <br> both start a new line, inline text joins the line it is on.
void ReadHtmlLines(const pugi::xml_node& node, std::vector<std::string>* lines,
                   bool fresh) {
    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        const std::string name = child.name();
        if (name.empty()) {  // a text node
            std::string text = child.value();
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last = text.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            text = text.substr(first, last - first + 1);
            if (fresh || lines->empty()) {
                lines->push_back(text);
                fresh = false;
            } else {
                lines->back() += text;
            }
            continue;
        }
        if (name == "br") {
            fresh = true;
            continue;
        }
        const bool block = name == "div" || name == "p";
        if (block && !lines->empty()) fresh = true;
        ReadHtmlLines(child, lines, fresh);
        if (block) fresh = true;
    }
}

// Reads the typography out of a <foreignObject>, deepest element last,
// which is the cascade order the browser applies.
void ReadHtmlLabel(const pugi::xml_node& node, HtmlLabel* label) {
    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        const std::string style = child.attribute("style").value();
        if (!style.empty()) {
            const std::string family = CssValue(style, "font-family");
            if (!family.empty()) {
                label->fontFamily = family;
                label->valid = true;
            }
            const std::string size = CssValue(style, "font-size");
            if (!size.empty()) {
                // "12px" and "12" both name a size; the unit is dropped by
                // the number reader.
                const float value = ReadLengthText(size);
                if (value > 0.0f) {
                    label->fontSize = value;
                    label->valid = true;
                }
            }
            const std::string color = CssValue(style, "color");
            if (!color.empty() && color != "transparent") {
                Color parsed;
                if (ReadCssColor(color, &parsed)) {
                    label->color = parsed;
                    label->hasColor = true;
                    label->valid = true;
                }
            }
            const std::string weight = CssValue(style, "font-weight");
            if (!weight.empty()) {
                label->bold = weight == "bold" || weight == "600" ||
                              weight == "700" || weight == "800" ||
                              weight == "900";
            }
            const std::string slant = CssValue(style, "font-style");
            if (!slant.empty()) {
                label->italic = slant == "italic" || slant == "oblique";
            }
            const std::string align = CssValue(style, "align-items");
            if (!align.empty()) label->centred = align.find("center") != std::string::npos;
            const std::string leading = CssValue(style, "line-height");
            if (!leading.empty()) {
                const float value = ReadLengthText(leading);
                if (value > 0.0f) label->lineHeight = value;
            }
        }
        ReadHtmlLabel(child, label);
    }
    if (!label->valid) return;
    std::vector<std::string> lines;
    ReadHtmlLines(node, &lines, false);
    if (!lines.empty()) label->lines = lines;
}

// Reads the style of a shape, which the group may have set.
SvgDoc::Style ReadStyle(const pugi::xml_node& node,
                          const SvgDoc::Style& base) {
    SvgDoc::Style style = base;

    const char* fill = node.attribute("fill").value();
    if (fill) {
        Color color;
        if (ReadColor(fill, &color)) {
            style.fill = color;
            style.hasFill = true;
        } else if (std::string(fill) == "none") {
            style.hasFill = false;
        }
    }

    const char* stroke = node.attribute("stroke").value();
    if (stroke) {
        Color color;
        if (ReadColor(stroke, &color)) {
            style.stroke = color;
            style.hasStroke = true;
        } else if (std::string(stroke) == "none") {
            style.hasStroke = false;
        }
    }

    float width = 0.0f;
    if (ReadNumber(node.attribute("stroke-width"), &width) && width > 0.0f) {
        style.strokeWidth = width;
    }

    float size = 0.0f;
    if (ReadNumber(node.attribute("font-size"), &size) && size > 0.0f) {
        style.fontSize = size;
    }

    const char* family = node.attribute("font-family").value();
    if (family && *family) style.fontFamily = family;

    const char* weight = node.attribute("font-weight").value();
    if (weight) {
        const std::string text(weight);
        style.bold = text == "bold" || text == "600" || text == "700" ||
                     text == "800" || text == "900";
    }

    const char* anchor = node.attribute("text-anchor").value();
    if (anchor && *anchor) style.anchor = anchor;

    const char* dash = node.attribute("stroke-dasharray").value();
    if (dash && *dash) {
        style.dash.clear();
        const char* p = dash;
        while (*p) {
            char* end = nullptr;
            const double value = std::strtod(p, &end);
            if (end == p) {
                ++p;
                continue;
            }
            if (value > 0.0) style.dash.push_back(static_cast<float>(value));
            p = end;
        }
        // An odd list repeats, the way the SVG spec says.
        if (style.dash.size() % 2 == 1) {
            const std::vector<float> once = style.dash;
            style.dash.insert(style.dash.end(), once.begin(), once.end());
        }
    }

    const char* fontStyle = node.attribute("font-style").value();
    if (fontStyle && *fontStyle) {
        const std::string text(fontStyle);
        style.italic = text == "italic" || text == "oblique";
    } else if (StyleSaysItalic(node.attribute("style").value())) {
        style.italic = true;
    }

    float opacity = 0.0f;
    if (ReadNumber(node.attribute("opacity"), &opacity)) {
        style.fill.a *= opacity;
        style.stroke.a *= opacity;
    }
    return style;
}

// Reads the numbers out of a list, the way the points of a polygon are
// written.
std::vector<float> ReadNumbers(const std::string& text) {
    std::vector<float> numbers;
    const char* p = text.c_str();
    while (*p) {
        char* end = nullptr;
        const double value = std::strtod(p, &end);
        if (end == p) {
            ++p;
            continue;
        }
        numbers.push_back(static_cast<float>(value));
        p = end;
    }
    return numbers;
}

// Turns the path of a shape into points.
//
// The commands are read one by one. The curves are cut into short
// lines, which is close enough at the size the documents are shown.
// Reads one number, skipping the separators SVG allows between numbers
// (whitespace and commas). Returns false at the end of the data or at a
// command letter.
bool ReadPathNumber(const std::string& text, size_t* at, float* out) {
    size_t p = *at;
    while (p < text.size() &&
           (text[p] == ',' ||
            std::isspace(static_cast<unsigned char>(text[p])))) {
        ++p;
    }
    if (p >= text.size()) return false;
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + p, &end);
    if (end == text.c_str() + p) return false;
    *out = static_cast<float>(value);
    *at = static_cast<size_t>(end - text.c_str());
    return true;
}

// Appends the ellipse arc of a path, cut into short lines.
//
// The centre, the radii and the sweep come from the SVG implementation notes
// (F.6.5), so a rounded elbow lands where a browser puts it. Without this the
// arc was dropped and the elbow came out sharp.
void AppendArc(std::vector<Point>* points, float x1, float y1, float rx,
               float ry, float rotationDegrees, bool largeArc, bool sweep,
               float x2, float y2) {
    if (!points) return;
    if (rx == 0.0f || ry == 0.0f || (x1 == x2 && y1 == y2)) {
        points->push_back(Point{x2, y2});
        return;
    }
    const float kPi = 3.14159265358979f;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const float phi = rotationDegrees * kPi / 180.0f;
    const float cs = std::cos(phi);
    const float sn = std::sin(phi);

    const float dx = (x1 - x2) * 0.5f;
    const float dy = (y1 - y2) * 0.5f;
    const float x1p = cs * dx + sn * dy;
    const float y1p = -sn * dx + cs * dy;

    // The radii are enlarged when they are too small to span the endpoints.
    const float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        const float factor = std::sqrt(lambda);
        rx *= factor;
        ry *= factor;
    }

    const float rx2 = rx * rx;
    const float ry2 = ry * ry;
    const float den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    float coefficient = 0.0f;
    if (den > 0.0f) {
        const float numerator = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        const float ratio = numerator / den;
        coefficient = std::sqrt(ratio > 0.0f ? ratio : 0.0f);
        if (largeArc == sweep) coefficient = -coefficient;
    }
    const float cxp = coefficient * (rx * y1p / ry);
    const float cyp = coefficient * (-ry * x1p / rx);
    const float cx = cs * cxp - sn * cyp + (x1 + x2) * 0.5f;
    const float cy = sn * cxp + cs * cyp + (y1 + y2) * 0.5f;

    auto angleBetween = [](float ux, float uy, float vx, float vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;
    const float startAngle = angleBetween(1.0f, 0.0f, ux, uy);
    float sweepAngle = angleBetween(ux, uy, vx, vy);
    if (!sweep && sweepAngle > 0.0f) sweepAngle -= 2.0f * kPi;
    else if (sweep && sweepAngle < 0.0f) sweepAngle += 2.0f * kPi;

    const int steps = std::max(
        4, static_cast<int>(std::ceil(std::fabs(sweepAngle) / (kPi / 16.0f))));
    for (int i = 1; i <= steps; ++i) {
        const float angle =
            startAngle + sweepAngle * (static_cast<float>(i) / steps);
        const float px = cx + rx * std::cos(angle) * cs - ry * std::sin(angle) * sn;
        const float py = cy + rx * std::cos(angle) * sn + ry * std::sin(angle) * cs;
        points->push_back(Point{px, py});
    }
    // Land exactly on the endpoint, so a cut arc does not drift.
    points->back() = Point{x2, y2};
}

std::vector<Point> ReadPath(const std::string& text, bool* closed) {
    if (closed) *closed = false;
    std::vector<Point> points;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;

    size_t at = 0;
    char command = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++at;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            command = c;
            ++at;
            // A close command carries no numbers, so it has to be handled
            // here: the switch below only runs when a number was read, and a
            // trailing "z" would otherwise never close the path.
            if (c == 'Z' || c == 'z') {
                if (closed) *closed = true;
                points.push_back(Point{startX, startY});
                currentX = startX;
                currentY = startY;
            }
            continue;
        }

        float value = 0.0f;
        if (!ReadPathNumber(text, &at, &value)) {
            ++at;
            continue;
        }

        switch (command) {
            case 'M':
            case 'm': {
                float y = 0.0f;
                if (!ReadPathNumber(text, &at, &y)) y = 0.0f;
                if (command == 'm') {
                    currentX += value;
                    currentY += y;
                } else {
                    currentX = value;
                    currentY = y;
                }
                startX = currentX;
                startY = currentY;
                points.push_back(Point{currentX, currentY});
                // The pairs that follow a move are lines.
                command = command == 'M' ? 'L' : 'l';
                break;
            }
            case 'L':
            case 'l': {
                float y = 0.0f;
                if (!ReadPathNumber(text, &at, &y)) y = 0.0f;
                if (command == 'l') {
                    currentX += value;
                    currentY += y;
                } else {
                    currentX = value;
                    currentY = y;
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'H':
            case 'h': {
                if (command == 'h') {
                    currentX += value;
                } else {
                    currentX = value;
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'V':
            case 'v': {
                if (command == 'v') {
                    currentY += value;
                } else {
                    currentY = value;
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'C':
            case 'c': {
                // The control points and the end point.
                float values[6] = {value, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 1; i < 6; ++i) {
                    if (!ReadPathNumber(text, &at, &values[i])) break;
                }
                float x1 = currentX;
                float y1 = currentY;
                if (command == 'c') {
                    x1 += values[0];
                    y1 += values[1];
                } else {
                    x1 = values[0];
                    y1 = values[1];
                }
                float x2 = x1;
                float y2 = y1;
                if (command == 'c') {
                    x2 += values[2];
                    y2 += values[3];
                } else {
                    x2 = values[2];
                    y2 = values[3];
                }
                float x3 = x2;
                float y3 = y2;
                if (command == 'c') {
                    x3 += values[4];
                    y3 += values[5];
                } else {
                    x3 = values[4];
                    y3 = values[5];
                }
                // The curve is cut into eight lines.
                const int kSteps = 8;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / kSteps;
                    const float u = 1.0f - t;
                    const float px = u * u * u * currentX +
                                     3.0f * u * u * t * x1 +
                                     3.0f * u * t * t * x2 + t * t * t * x3;
                    const float py = u * u * u * currentY +
                                     3.0f * u * u * t * y1 +
                                     3.0f * u * t * t * y2 + t * t * t * y3;
                    points.push_back(Point{px, py});
                }
                currentX = x3;
                currentY = y3;
                break;
            }
            case 'Q':
            case 'q': {
                float values[4] = {value, 0.0f, 0.0f, 0.0f};
                for (int i = 1; i < 4; ++i) {
                    if (!ReadPathNumber(text, &at, &values[i])) break;
                }
                float x1 = values[0];
                float y1 = values[1];
                float x2 = values[2];
                float y2 = values[3];
                if (command == 'q') {
                    x2 += currentX;
                    y2 += currentY;
                }
                const int kSteps = 8;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / kSteps;
                    const float u = 1.0f - t;
                    const float px = u * u * currentX + 2.0f * u * t * x1 +
                                     t * t * x2;
                    const float py = u * u * currentY + 2.0f * u * t * y1 +
                                     t * t * y2;
                    points.push_back(Point{px, py});
                }
                currentX = x2;
                currentY = y2;
                break;
            }
            case 'A':
            case 'a': {
                // rx ry rotation large-arc sweep x y
                float values[7] = {value, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                bool complete = true;
                for (int i = 1; i < 7; ++i) {
                    if (!ReadPathNumber(text, &at, &values[i])) {
                        complete = false;
                        break;
                    }
                }
                if (!complete) break;
                float x = values[5];
                float y = values[6];
                if (command == 'a') {
                    x += currentX;
                    y += currentY;
                }
                AppendArc(&points, currentX, currentY, values[0], values[1],
                          values[2], values[3] != 0.0f, values[4] != 0.0f, x, y);
                currentX = x;
                currentY = y;
                break;
            }
            case 'Z':
            case 'z':
                break;
            default:
                break;
        }
    }
    return points;
}

// Reads the shapes out of a node and its children.
void ReadShapes(const pugi::xml_node& node, const SvgDoc::Style& style,
                const SvgDoc::Placement& placement, const HtmlLabel* html,
                std::vector<SvgDoc::Shape>* out);

void ReadShapes(const pugi::xml_node& node, const SvgDoc::Style& style,
                const SvgDoc::Placement& placement, const HtmlLabel* html,
                std::vector<SvgDoc::Shape>* out) {
    SvgDoc::Style childStyle = ReadStyle(node, style);
    const SvgDoc::Placement childPlacement = ReadPlacement(node, placement);

    const std::string name = node.name();

    // Containers that define things rather than draw them. Their content is
    // reached through a reference, so walking into them would draw a copy of
    // every marker at the origin.
    if (name == "defs" || name == "marker" || name == "symbol" ||
        name == "clipPath" || name == "mask" || name == "pattern" ||
        name == "linearGradient" || name == "radialGradient" ||
        name == "filter" || name == "style" || name == "title" ||
        name == "desc" || name == "metadata") {
        return;
    }

    // A switch draws its first branch whose feature tests pass. This
    // renderer cannot lay out HTML, so a <foreignObject> branch is passed
    // over and the next branch, in a diagram export always the <text>
    // fallback, is drawn with the HTML's own typography. When the first
    // branch is something that can be drawn, even an empty
    // <g requiredFeatures=".."/>, the switch ends there: everything after
    // it is a fallback, and a browser draws none of it. The draw.io
    // "Text is not SVG - cannot display" notice lives in exactly such a
    // tail switch.
    if (name == "switch") {
        HtmlLabel label;
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            const std::string branch = child.name();
            if (branch.empty()) continue;  // a text node between branches
            if (branch == "foreignObject") {
                ReadHtmlLabel(child, &label);
                continue;
            }
            // An empty group with no feature test is "no content yet" in the
            // older exports, which put one before the real branch: taking it
            // would drop the label. With a test it is a branch a browser
            // takes, and the rest of the switch is never reached.
            if (branch == "g" && child.first_child().empty() &&
                child.attribute("requiredFeatures").empty() &&
                child.attribute("requiredExtensions").empty() &&
                child.attribute("systemLanguage").empty()) {
                continue;
            }
            ReadShapes(child, childStyle, childPlacement,
                       label.valid ? &label : nullptr, out);
            break;
        }
        return;
    }

    SvgDoc::Shape shape;
    // A label the exporter wrapped line by line: every <tspan> carries its
    // own x and y. Joining them into one string ran the wrap together and
    // let a long label spill out of its box.
    std::vector<SvgDoc::Shape> tspanLines;

    if (name == "rect") {
        const float x = ReadLength(node.attribute("x"), 0.0f);
        const float y = ReadLength(node.attribute("y"), 0.0f);
        const float w = ReadLength(node.attribute("width"), 0.0f);
        const float h = ReadLength(node.attribute("height"), 0.0f);
        if (w > 0.0f && h > 0.0f) {
            shape.kind = SvgDoc::Kind::Rect;
            shape.points.push_back(Point{x, y});
            shape.points.push_back(Point{x + w, y + h});
            // A rounded rectangle: rx and ry default to each other, and a
            // diagram's boxes carry rx="10".
            const float rx = ReadLength(node.attribute("rx"), 0.0f);
            const float ry = ReadLength(node.attribute("ry"), 0.0f);
            shape.radiusX = rx > 0.0f ? rx : ry;
            shape.radiusY = ry > 0.0f ? ry : rx;
        }
    } else if (name == "circle") {
        const float cx = ReadLength(node.attribute("cx"), 0.0f);
        const float cy = ReadLength(node.attribute("cy"), 0.0f);
        const float r = ReadLength(node.attribute("r"), 0.0f);
        if (r > 0.0f) {
            shape.kind = SvgDoc::Kind::Ellipse;
            shape.points.push_back(Point{cx, cy});
            shape.radiusX = r;
            shape.radiusY = r;
        }
    } else if (name == "ellipse") {
        const float cx = ReadLength(node.attribute("cx"), 0.0f);
        const float cy = ReadLength(node.attribute("cy"), 0.0f);
        const float rx = ReadLength(node.attribute("rx"), 0.0f);
        const float ry = ReadLength(node.attribute("ry"), 0.0f);
        if (rx > 0.0f && ry > 0.0f) {
            shape.kind = SvgDoc::Kind::Ellipse;
            shape.points.push_back(Point{cx, cy});
            shape.radiusX = rx;
            shape.radiusY = ry;
        }
    } else if (name == "line") {
        const float x1 = ReadLength(node.attribute("x1"), 0.0f);
        const float y1 = ReadLength(node.attribute("y1"), 0.0f);
        const float x2 = ReadLength(node.attribute("x2"), 0.0f);
        const float y2 = ReadLength(node.attribute("y2"), 0.0f);
        shape.kind = SvgDoc::Kind::Line;
        shape.points.push_back(Point{x1, y1});
        shape.points.push_back(Point{x2, y2});
    } else if (name == "polyline" || name == "polygon") {
        const std::vector<float> numbers =
            ReadNumbers(node.attribute("points").value());
        shape.kind = SvgDoc::Kind::Polygon;
        for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
            shape.points.push_back(Point{numbers[i], numbers[i + 1]});
        }
        if (name == "polyline") shape.kind = SvgDoc::Kind::Line;
    } else if (name == "path") {
        shape.kind = SvgDoc::Kind::Polygon;
        shape.points = ReadPath(node.attribute("d").value(), &shape.closed);
    } else if (name == "text") {
        const float x = ReadLength(node.attribute("x"), 0.0f);
        const float y = ReadLength(node.attribute("y"), 0.0f);
        shape.kind = SvgDoc::Kind::Text;
        shape.points.push_back(Point{x, y});
        std::string content = node.child_value();
        // The text of an element can be spread over several nodes.
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            content += child.child_value();
        }
        shape.text = content;

        // One line per positioned <tspan>: that is how a wrapped label is
        // written, and the browser draws each one where its x and y say.
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            if (std::string(child.name()) != "tspan") continue;
            const bool hasX = !child.attribute("x").empty();
            const bool hasY = !child.attribute("y").empty();
            if (!hasX && !hasY) continue;
            SvgDoc::Shape line;
            line.kind = SvgDoc::Kind::Text;
            line.points.push_back(Point{
                hasX ? ReadLength(child.attribute("x"), x) : x,
                hasY ? ReadLength(child.attribute("y"), y) : y});
            line.text = child.child_value();
            if (line.text.empty()) continue;
            tspanLines.push_back(line);
        }
        if (!tspanLines.empty()) shape.text.clear();

        // The HTML label the switch passed over is what a browser draws.
        if (html && html->valid) {
            if (!html->fontFamily.empty()) {
                childStyle.fontFamily = html->fontFamily;
            }
            if (html->fontSize > 0.0f) childStyle.fontSize = html->fontSize;
            if (html->hasColor) {
                childStyle.fill = html->color;
                childStyle.hasFill = true;
            }
            childStyle.bold = html->bold;
            childStyle.italic = html->italic;

            // The browser breaks the HTML's lines, not the exporter's
            // tspans: a short label is one HTML line though it is written
            // as two tspans, and drawing both added a line.
            if (!html->lines.empty()) {
                const float size =
                    childStyle.fontSize > 0.0f ? childStyle.fontSize : 12.0f;
                float leading = html->lineHeight;
                if (leading > 0.0f && leading < 4.0f) leading *= size;
                if (leading <= 0.0f) leading = size * 1.2f;
                float first = y;
                if (!tspanLines.empty()) first = tspanLines.front().points[0].y;
                if (html->centred && !tspanLines.empty()) {
                    // The block is centred on the box, so a line count
                    // other than the tspans' one moves the whole block.
                    const float last = tspanLines.back().points[0].y;
                    first = (first + last) * 0.5f -
                            (static_cast<float>(html->lines.size()) - 1.0f) *
                                leading * 0.5f;
                }
                tspanLines.clear();
                for (size_t i = 0; i < html->lines.size(); ++i) {
                    SvgDoc::Shape line;
                    line.kind = SvgDoc::Kind::Text;
                    line.points.push_back(
                        Point{x, first + static_cast<float>(i) * leading});
                    line.text = html->lines[i];
                    tspanLines.push_back(line);
                }
                shape.text.clear();
            }
        }
    } else {
        shape.kind = SvgDoc::Kind::Polygon;
        shape.points.clear();
    }

    if (!tspanLines.empty()) {
        for (SvgDoc::Shape& line : tspanLines) {
            line.style = childStyle;
            line.placement = childPlacement;
            out->push_back(line);
        }
    } else if (!shape.points.empty() || name == "text") {
        shape.style = childStyle;
        shape.placement = childPlacement;
        out->push_back(shape);
    }

    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        ReadShapes(child, childStyle, childPlacement, nullptr, out);
    }
}

}  // namespace

bool SvgDoc::Load(const std::string& xml) {
    shapes_.clear();
    width_ = 0.0f;
    height_ = 0.0f;

    // This renderer has no <marker> support either, so every arrowhead is
    // rewritten into an explicit group before the XML is parsed. The rewrite
    // is geometric only and leaves a document without markers untouched.
    const std::string expanded = svg::ExpandMarkers(xml);

    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        expanded.data(), expanded.size(),
        pugi::parse_default | pugi::parse_escapes);
    if (!result) return false;

    // The root of the document is the element named svg.
    pugi::xml_node root;
    for (pugi::xml_node child = document.first_child(); child;
         child = child.next_sibling()) {
        if (std::string(child.name()) == "svg") {
            root = child;
            break;
        }
    }
    if (!root) return false;

    // The size of the document comes from the width and the height, and
    // the view box says which part of it is shown.
    const float width = ReadLength(root.attribute("width"), 0.0f);
    const float height = ReadLength(root.attribute("height"), 0.0f);

    float viewBoxX = 0.0f;
    float viewBoxY = 0.0f;
    float viewBoxWidth = 0.0f;
    float viewBoxHeight = 0.0f;
    const std::vector<float> box =
        ReadNumbers(root.attribute("viewBox").value());
    if (box.size() >= 4) {
        viewBoxX = box[0];
        viewBoxY = box[1];
        viewBoxWidth = box[2];
        viewBoxHeight = box[3];
    }

    if (viewBoxWidth > 0.0f && viewBoxHeight > 0.0f) {
        viewBoxX_ = viewBoxX;
        viewBoxY_ = viewBoxY;
        viewBoxWidth_ = viewBoxWidth;
        viewBoxHeight_ = viewBoxHeight;
        width_ = width > 0.0f ? width : viewBoxWidth;
        height_ = height > 0.0f ? height : viewBoxHeight;
    } else {
        width_ = width;
        height_ = height;
    }

    if (width_ <= 0.0f || height_ <= 0.0f) return false;

    SvgDoc::Style style;
    style.fill = Color{0.0f, 0.0f, 0.0f, 1.0f};
    style.hasFill = true;
    SvgDoc::Placement placement;
    ReadShapes(root, style, placement, nullptr, &shapes_);
    return !shapes_.empty();
}

namespace {

// Strokes a polyline with a dash pattern. The pattern is in user units and
// scales with the shape, the way a browser scales it. Each dash is its own
// line, which is what the render target can draw.
void StrokeDashed(RenderTarget* target, const std::vector<Point>& points,
                  bool closed, const std::vector<float>& dash, float scale,
                  const Color& color, float lineWidth) {
    if (!target || dash.empty() || points.size() < 2) return;
    std::vector<float> pattern;
    pattern.reserve(dash.size());
    for (float d : dash) pattern.push_back(d * scale);
    float total = 0.0f;
    for (float d : pattern) total += d;
    if (!(total > 0.0f)) return;

    size_t index = 0;
    float remaining = pattern[0];
    bool on = true;
    const size_t segments = closed ? points.size() : points.size() - 1;
    for (size_t i = 0; i < segments; ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0f)) continue;
        dx /= length;
        dy /= length;
        float pos = 0.0f;
        while (pos < length) {
            const float step = std::min(remaining, length - pos);
            if (on) {
                target->StrokeLine(a.x + dx * pos, a.y + dy * pos,
                                   a.x + dx * (pos + step),
                                   a.y + dy * (pos + step), color, lineWidth);
            }
            pos += step;
            remaining -= step;
            if (remaining <= 1e-4f) {
                index = (index + 1) % pattern.size();
                remaining = pattern[index];
                on = !on;
            }
        }
    }
}

}  // namespace

void SvgDoc::Draw(RenderTarget* target, TextEngine* engine, float x, float y,
                   float w, float h) const {
    if (!target || width_ <= 0.0f || height_ <= 0.0f) return;

    // The document is scaled into the rectangle, and the shape of it is
    // kept.
    const float scale = std::min(w / width_, h / height_);
    const float offsetX = x + (w - width_ * scale) * 0.5f;
    const float offsetY = y + (h - height_ * scale) * 0.5f;

    // The view box says which part of the document is shown.
    const float sourceX = viewBoxWidth_ > 0.0f ? viewBoxX_ : 0.0f;
    const float sourceY = viewBoxHeight_ > 0.0f ? viewBoxY_ : 0.0f;
    const float sourceWidth = viewBoxWidth_ > 0.0f ? viewBoxWidth_ : width_;
    const float sourceHeight = viewBoxHeight_ > 0.0f ? viewBoxHeight_ : height_;
    const float viewScale = std::min(width_ / sourceWidth, height_ / sourceHeight);

    for (const Shape& shape : shapes_) {
        if (shape.points.empty()) continue;

        const float shapeScale = scale * viewScale;
        // The shape's own transform multiplies its stroke widths and its
        // corner radii as well as its geometry.
        const float strokeFactor = shapeScale * MatrixScale(shape.placement);

        auto placeShape = [&](const Point& point) {
            const float x = shape.placement.a * point.x +
                            shape.placement.c * point.y + shape.placement.e;
            const float y = shape.placement.b * point.x +
                            shape.placement.d * point.y + shape.placement.f;
            return Point{offsetX + (x - sourceX) * shapeScale,
                         offsetY + (y - sourceY) * shapeScale};
        };

        switch (shape.kind) {
            case Kind::Rect: {
                const Point topLeft = placeShape(shape.points[0]);
                const Point bottomRight = placeShape(shape.points[1]);
                Rect rect{topLeft.x, topLeft.y, bottomRight.x - topLeft.x,
                          bottomRight.y - topLeft.y};
                const float radius = shape.radiusX * strokeFactor;
                if (radius > 0.0f) {
                    if (shape.style.hasFill) {
                        target->FillRoundedRect(rect, radius, shape.style.fill);
                    }
                    if (shape.style.hasStroke) {
                        target->StrokeRoundedRect(
                            rect, radius, shape.style.stroke,
                            shape.style.strokeWidth * strokeFactor);
                    }
                } else {
                    if (shape.style.hasFill) {
                        target->FillRect(rect, shape.style.fill);
                    }
                    if (shape.style.hasStroke) {
                        target->StrokeRect(rect, shape.style.stroke,
                                           shape.style.strokeWidth * strokeFactor);
                    }
                }
                break;
            }
            case Kind::Ellipse: {
                const Point centre = placeShape(shape.points[0]);
                Rect rect{centre.x - shape.radiusX * strokeFactor,
                          centre.y - shape.radiusY * strokeFactor,
                          shape.radiusX * 2.0f * strokeFactor,
                          shape.radiusY * 2.0f * strokeFactor};
                if (shape.style.hasFill) {
                    target->FillEllipse(rect, shape.style.fill);
                }
                if (shape.style.hasStroke) {
                    target->StrokeEllipse(rect, shape.style.stroke,
                                          shape.style.strokeWidth * strokeFactor);
                }
                break;
            }
            case Kind::Line: {
                if (shape.points.size() < 2) break;
                const Point from = placeShape(shape.points[0]);
                const Point to = placeShape(shape.points[1]);
                if (!shape.style.dash.empty()) {
                    StrokeDashed(target, {from, to}, false, shape.style.dash,
                                 strokeFactor, shape.style.stroke,
                                 shape.style.strokeWidth * strokeFactor);
                } else {
                    target->StrokeLine(from.x, from.y, to.x, to.y,
                                       shape.style.stroke,
                                       shape.style.strokeWidth * strokeFactor);
                }
                break;
            }
            case Kind::Polygon: {
                std::vector<Point> points;
                points.reserve(shape.points.size());
                for (const Point& point : shape.points) {
                    points.push_back(placeShape(point));
                }
                if (points.size() < 2) break;
                if (shape.style.hasFill) {
                    target->FillPolygon(points, shape.style.fill);
                }
                if (shape.style.hasStroke) {
                    if (!shape.style.dash.empty()) {
                        StrokeDashed(target, points, shape.closed,
                                     shape.style.dash, strokeFactor,
                                     shape.style.stroke,
                                     shape.style.strokeWidth * strokeFactor);
                    } else {
                        target->StrokePolygon(
                            points, shape.style.stroke,
                            shape.style.strokeWidth * strokeFactor,
                            shape.closed);
                    }
                }
                break;
            }
            case Kind::Text: {
                const Point anchor = placeShape(shape.points[0]);
                FontSpec font;
                font.family = shape.style.fontFamily.empty()
                                  ? std::string("Segoe UI")
                                  : FirstFamily(shape.style.fontFamily);
                font.size = shape.style.fontSize * strokeFactor;
                font.bold = shape.style.bold;
                font.italic = shape.style.italic;
                const Color color = shape.style.hasFill ? shape.style.fill
                                                        : Color{0.0f, 0.0f, 0.0f, 1.0f};
                // text-anchor places the label relative to its own width, so
                // the width has to be measured with the same engine that
                // draws it. A browser centres almost every diagram label.
                float tx = anchor.x;
                if (engine && !shape.text.empty() &&
                    !shape.style.anchor.empty() &&
                    shape.style.anchor != "start") {
                    GlyphRun run;
                    run.text = shape.text;
                    run.font = font;
                    const std::vector<TextLine> lines =
                        engine->MeasureWrapped({run}, 1.0e9f, 0.0f);
                    if (!lines.empty()) {
                        if (shape.style.anchor == "middle") {
                            tx -= lines.front().width * 0.5f;
                        } else if (shape.style.anchor == "end") {
                            tx -= lines.front().width;
                        }
                    }
                }
                target->DrawText(shape.text, font, tx, anchor.y, color);
                break;
            }
        }
    }
}

}  // namespace platform
