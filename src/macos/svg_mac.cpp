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

namespace mac {
namespace {

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
std::vector<Point> ReadPath(const std::string& text) {
    std::vector<Point> points;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;

    size_t at = 0;
    char command = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++at;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            command = c;
            ++at;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(text.c_str() + at, &end);
        if (end == text.c_str() + at) {
            ++at;
            continue;
        }
        at = static_cast<size_t>(end - text.c_str());

        switch (command) {
            case 'M':
            case 'm': {
                float x = static_cast<float>(value);
                float y = 0.0f;
                if (at < text.size() && text[at] != '-' && text[at] != '+' &&
                    text[at] != '.' && !std::isspace(static_cast<unsigned char>(text[at]))) {
                    char* end2 = nullptr;
                    const double yValue = std::strtod(text.c_str() + at, &end2);
                    y = static_cast<float>(yValue);
                    at = static_cast<size_t>(end2 - text.c_str());
                }
                if (command == 'm') {
                    currentX += x;
                    currentY += y;
                } else {
                    currentX = x;
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
                float x = static_cast<float>(value);
                float y = 0.0f;
                if (at < text.size() && text[at] != '-' && text[at] != '+' &&
                    text[at] != '.' && !std::isspace(static_cast<unsigned char>(text[at]))) {
                    char* end2 = nullptr;
                    const double yValue = std::strtod(text.c_str() + at, &end2);
                    y = static_cast<float>(yValue);
                    at = static_cast<size_t>(end2 - text.c_str());
                }
                if (command == 'l') {
                    currentX += x;
                    currentY += y;
                } else {
                    currentX = x;
                    currentY = y;
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'H':
            case 'h': {
                if (command == 'h') {
                    currentX += static_cast<float>(value);
                } else {
                    currentX = static_cast<float>(value);
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'V':
            case 'v': {
                if (command == 'v') {
                    currentY += static_cast<float>(value);
                } else {
                    currentY = static_cast<float>(value);
                }
                points.push_back(Point{currentX, currentY});
                break;
            }
            case 'C':
            case 'c': {
                // The control points and the end point.
                float values[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 6; ++i) {
                    if (at >= text.size()) break;
                    char* end2 = nullptr;
                    const double piece = std::strtod(text.c_str() + at, &end2);
                    if (end2 == text.c_str() + at) break;
                    values[i] = static_cast<float>(piece);
                    at = static_cast<size_t>(end2 - text.c_str());
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
                float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 4; ++i) {
                    if (at >= text.size()) break;
                    char* end2 = nullptr;
                    const double piece = std::strtod(text.c_str() + at, &end2);
                    if (end2 == text.c_str() + at) break;
                    values[i] = static_cast<float>(piece);
                    at = static_cast<size_t>(end2 - text.c_str());
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
            case 'z': {
                points.push_back(Point{startX, startY});
                currentX = startX;
                currentY = startY;
                break;
            }
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
        shape.points = ReadPath(node.attribute("d").value());
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

    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        xml.data(), xml.size(), pugi::parse_default | pugi::parse_escapes);
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

void SvgDoc::Draw(RenderTarget* target, float x, float y, float w,
                   float h) const {
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
                if (shape.style.hasFill) target->FillRect(rect, shape.style.fill);
                if (shape.style.hasStroke) {
                    target->StrokeRect(rect, shape.style.stroke,
                                       shape.style.strokeWidth * shapeScale);
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
                target->StrokeLine(from.x, from.y, to.x, to.y, shape.style.stroke,
                                    shape.style.strokeWidth * shapeScale);
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
                    target->StrokePolygon(points, shape.style.stroke,
                                          shape.style.strokeWidth * shapeScale,
                                          true);
                }
                break;
            }
            case Kind::Text: {
                const Point anchor = placeShape(shape.points[0]);
                FontSpec font;
                font.family = shape.style.fontFamily.empty()
                                  ? std::string("Helvetica")
                                  : shape.style.fontFamily;
                font.size = shape.style.fontSize * shapeScale;
                font.bold = shape.style.bold;
                const Color color = shape.style.hasFill ? shape.style.fill
                                                        : Color{0.0f, 0.0f, 0.0f, 1.0f};
                target->DrawText(shape.text, font, anchor.x, anchor.y, color);
                break;
            }
        }
    }
}

}  // namespace mac
