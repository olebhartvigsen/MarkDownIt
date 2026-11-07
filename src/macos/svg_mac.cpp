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

namespace mac {
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

// Reads the placement of a group or a shape.
//
// The transform the documents use is a translate and a scale, and
// sometimes a rotation, which is left out because the shapes that use
// it are rare.
SvgDoc::Placement ReadPlacement(const pugi::xml_node& node,
                                const SvgDoc::Placement& base) {
    SvgDoc::Placement placement = base;
    const char* transform = node.attribute("transform").value();
    if (!transform) return placement;

    std::string text(transform);
    size_t at = 0;
    while (at < text.size()) {
        const size_t open = text.find('(', at);
        if (open == std::string::npos) break;
        const size_t close = text.find(')', open);
        if (close == std::string::npos) break;
        const std::string name = text.substr(at, open - at);
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

        if (name.find("translate") != std::string::npos && !numbers.empty()) {
            placement.translateX += numbers[0];
            placement.translateY += numbers.size() > 1 ? numbers[1] : 0.0f;
        } else if (name.find("scale") != std::string::npos && !numbers.empty()) {
            const float factor = numbers[0];
            placement.translateX *= factor;
            placement.translateY *= factor;
            placement.scale *= factor;
        }
        at = close + 1;
    }
    return placement;
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
                const SvgDoc::Placement& placement,
                std::vector<SvgDoc::Shape>* out);

void ReadShapes(const pugi::xml_node& node, const SvgDoc::Style& style,
                const SvgDoc::Placement& placement,
                std::vector<SvgDoc::Shape>* out) {
    const SvgDoc::Style childStyle = ReadStyle(node, style);
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

    SvgDoc::Shape shape;

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
    } else {
        shape.kind = SvgDoc::Kind::Polygon;
        shape.points.clear();
    }

    if (!shape.points.empty() || name == "text") {
        shape.style = childStyle;
        shape.placement = childPlacement;
        out->push_back(shape);
    }

    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        ReadShapes(child, childStyle, childPlacement, out);
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
    ReadShapes(root, style, placement, &shapes_);
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

        const float shapeScale = scale * viewScale * shape.placement.scale;
        const float translateX = shape.placement.translateX * shapeScale;
        const float translateY = shape.placement.translateY * shapeScale;

        auto placeShape = [&](const Point& point) {
            return Point{offsetX + (point.x - sourceX) * shapeScale + translateX,
                         offsetY + (point.y - sourceY) * shapeScale + translateY};
        };

        switch (shape.kind) {
            case Kind::Rect: {
                const Point topLeft = placeShape(shape.points[0]);
                const Point bottomRight = placeShape(shape.points[1]);
                Rect rect{topLeft.x, topLeft.y, bottomRight.x - topLeft.x,
                          bottomRight.y - topLeft.y};
                const float radius = shape.radiusX * shapeScale;
                if (radius > 0.0f) {
                    if (shape.style.hasFill) {
                        target->FillRoundedRect(rect, radius, shape.style.fill);
                    }
                    if (shape.style.hasStroke) {
                        target->StrokeRoundedRect(
                            rect, radius, shape.style.stroke,
                            shape.style.strokeWidth * shapeScale);
                    }
                } else {
                    if (shape.style.hasFill) {
                        target->FillRect(rect, shape.style.fill);
                    }
                    if (shape.style.hasStroke) {
                        target->StrokeRect(rect, shape.style.stroke,
                                           shape.style.strokeWidth * shapeScale);
                    }
                }
                break;
            }
            case Kind::Ellipse: {
                const Point centre = placeShape(shape.points[0]);
                Rect rect{centre.x - shape.radiusX * shapeScale,
                          centre.y - shape.radiusY * shapeScale,
                          shape.radiusX * 2.0f * shapeScale,
                          shape.radiusY * 2.0f * shapeScale};
                if (shape.style.hasFill) {
                    target->FillEllipse(rect, shape.style.fill);
                }
                if (shape.style.hasStroke) {
                    target->StrokeEllipse(rect, shape.style.stroke,
                                          shape.style.strokeWidth * shapeScale);
                }
                break;
            }
            case Kind::Line: {
                if (shape.points.size() < 2) break;
                const Point from = placeShape(shape.points[0]);
                const Point to = placeShape(shape.points[1]);
                if (!shape.style.dash.empty()) {
                    StrokeDashed(target, {from, to}, false, shape.style.dash,
                                 shapeScale, shape.style.stroke,
                                 shape.style.strokeWidth * shapeScale);
                } else {
                    target->StrokeLine(from.x, from.y, to.x, to.y,
                                       shape.style.stroke,
                                       shape.style.strokeWidth * shapeScale);
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
                                     shape.style.dash, shapeScale,
                                     shape.style.stroke,
                                     shape.style.strokeWidth * shapeScale);
                    } else {
                        target->StrokePolygon(
                            points, shape.style.stroke,
                            shape.style.strokeWidth * shapeScale,
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
                font.size = shape.style.fontSize * shapeScale;
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

}  // namespace mac
