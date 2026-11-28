// The mermaid diagram painter, shared by both ports.
//
// It paints the shared layout through the RenderTarget and TextEngine
// seams. Everything the diagram needs is a seam primitive: paths are
// polylines, brushes are colors, and text is measured by the engine the
// caller passes in. The numbers are mermaid's own, so both ports draw
// the same picture from the same file.

#include "mermaid/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mermaid/layout.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"

namespace platform {
namespace {

// The mermaid default-theme pie palette (pie1..pie12).
constexpr uint32_t kPieColors[] = {
    0xECECFF,  // pie1
    0xFFFFDE,  // pie2
    0xB5FF20,  // pie3
    0xB9B9FF,  // pie4
    0xFFFF45,  // pie5
    0xD7FF86,  // pie6
    0xFF86FF,  // pie7
    0x20FFFF,  // pie8
    0xFF2020,  // pie9
    0xFF20FF,  // pie10
    0x20FF8F,  // pie11
    0xFF5353,  // pie12
};
constexpr size_t kPieColorCount = sizeof(kPieColors) / sizeof(kPieColors[0]);

// Mermaid's pie text sizes (pieSectionTextSize, pieLegendTextSize,
// pieTitleTextSize) in the layout's own units. The document body font is
// larger than any of them, which is what left the percents and the
// legend oversized.
constexpr float kPieSectionFontSize = 17.0f;
constexpr float kPieLegendFontSize = 17.0f;
constexpr float kPieTitleFontSize = 25.0f;

// The legend row pitch and the gap after the swatch, mermaid's
// LEGEND_RECT_SIZE 18 plus LEGEND_SPACING 4.
constexpr float kPieLegendPitch = 22.0f;
constexpr float kPieLegendGap = 4.0f;

// The pie's outline weight: one hairline at the chart's scale. Mermaid
// states one 2px weight for the whole outline; drawing it once per edge
// is what made it read as a heavy black border.
constexpr float kPieStrokeWidth = 1.0f;

static_assert(sizeof(kPieColors) / sizeof(kPieColors[0]) ==
                  static_cast<size_t>(mermaid::kPieColorCount),
              "the painter palette and the layout's colour count must agree");

// The sequence palette, matching the mermaid sequence default theme.
// The sequence diagram takes its colours from the palette's diagram
// tokens (theme.h), so it follows the appearance the way the flowchart
// does. The light values there are the ones this file used to hold.

// The height of an actor box, in mermaid's own units.
constexpr float kActorBoxHeight = 65.0f;

// The size of the autonumber digits, in mermaid's own units.
constexpr float kNumberFontSize = 12.0f;

constexpr float kPi = 3.14159265358979323846f;

// The flowchart's own numbers, the ones its Windows body drew with. They
// are mermaid's geometry, not the page's spacing, so they live here
// rather than in the theme's block tokens.
constexpr float kFlowLaneInsetX = 6.0f;
constexpr float kFlowLaneInsetY = 2.0f;
constexpr float kFlowLaneTitleHeight = 20.0f;
constexpr float kFlowNodeRadius = 6.0f;
constexpr float kFlowEdgeStroke = 1.5f;
constexpr float kFlowEdgeStrokeThick = 3.5f;
constexpr float kFlowEdgeLabelWidth = 220.0f;
constexpr float kFlowEdgeLabelHeight = 28.0f;
constexpr float kFlowEdgeLabelPad = 2.0f;
constexpr float kFlowArrowLength = 10.0f;
constexpr float kFlowArrowHalfWidth = 6.0f;

// A dotted edge's dash and gap, in stroke-width multiples.
constexpr float kFlowDash[2] = {1.0f, 2.0f};

// A colour from the theme.
Color ToColor(const ThemeColor& color) {
    Color out;
    out.r = color.r;
    out.g = color.g;
    out.b = color.b;
    out.a = color.a;
    return out;
}

// A colour from a 0xRRGGBB value.
Color Rgb(uint32_t rgb) { return ToColor(MakeColor(rgb)); }

// A colour behind a hairline. Mermaid strokes every slice, so a boundary
// is drawn in one ink; it has to separate the two fills it lies between,
// including two neighbours that ended up with the same fill. The dark and
// the light ink are tried against both fills and the one with the larger
// worst case wins, so a pale pair takes the dark line and a saturated
// pair the light one.
Color DividerInk(const Color& left, const Color& right) {
    const Color dark = Rgb(0x333333);
    const Color light = Rgb(0xF5F5F5);
    const auto luma = [](const Color& c) {
        return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    };
    const auto worst = [&](const Color& ink) {
        return std::min(std::fabs(luma(ink) - luma(left)),
                        std::fabs(luma(ink) - luma(right)));
    };
    return worst(dark) >= worst(light) ? dark : light;
}

// The measured size of one line of text.
struct TextSize {
    float width = 0.0f;
    float height = 0.0f;
    float ascent = 0.0f;
};

// Measures one line of text.
//
// The engine is asked for one unbounded line, so the result is the size
// of the text as it is drawn.
TextSize MeasureText(TextEngine* engine, const std::string& utf8,
                      const FontSpec& font) {
    TextSize size;
    if (!engine || utf8.empty()) return size;
    GlyphRun run;
    run.text = utf8;
    run.font = font;
    const std::vector<TextLine> lines =
        engine->MeasureWrapped({run}, 1.0e9f, 0.0f);
    if (lines.empty()) return size;
    size.width = lines.front().width;
    size.height = lines.front().height;
    size.ascent = lines.front().ascent;
    return size;
}

// Draws a line of text centred on the point.
void DrawCentered(RenderTarget* target, TextEngine* engine,
                  const std::string& text, const FontSpec& font, float cx,
                  float cy, const Color& color) {
    if (!target || text.empty()) return;
    const TextSize size = MeasureText(engine, text, font);
    target->DrawText(text, font, cx - size.width * 0.5f,
                     cy - size.height * 0.5f + size.ascent, color);
}

// Draws a line of text with its left edge on the x and its middle on the y.
void DrawLeftCentered(RenderTarget* target, TextEngine* engine,
                      const std::string& text, const FontSpec& font, float x,
                      float cy, const Color& color) {
    if (!target || text.empty()) return;
    const TextSize size = MeasureText(engine, text, font);
    target->DrawText(text, font, x, cy - size.height * 0.5f + size.ascent,
                     color);
}

// Draws a line of text centred on the x, with its top on the y.
void DrawCenteredTop(RenderTarget* target, TextEngine* engine,
                     const std::string& text, const FontSpec& font, float cx,
                     float y, const Color& color) {
    if (!target || text.empty()) return;
    const TextSize size = MeasureText(engine, text, font);
    target->DrawText(text, font, cx - size.width * 0.5f, y + size.ascent,
                     color);
}

// Draws a line of text centred on the x, with its baseline on the y.
void DrawBaseline(RenderTarget* target, TextEngine* engine,
                  const std::string& text, const FontSpec& font, float cx,
                  float baselineY, const Color& color) {
    if (!target || text.empty()) return;
    const TextSize size = MeasureText(engine, text, font);
    target->DrawText(text, font, cx - size.width * 0.5f, baselineY, color);
}

// Strokes a closed polygon.
void StrokePolygon(RenderTarget* target, const std::vector<Point>& points,
                   const Color& color, float lineWidth) {
    if (!target || points.size() < 2) return;
    for (size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        target->StrokeLine(a.x, a.y, b.x, b.y, color, lineWidth);
    }
}

// Strokes a rounded rectangle.
//
// The seam has no rounded stroke, so the shape is stroked as a polyline
// with the corners approximated by short segments. The corners are the
// only difference from a real rounded stroke, and at the widths the
// diagrams use they do not show.
void StrokeRoundedRect(RenderTarget* target, const Rect& rect, float radius,
                       const Color& color, float lineWidth) {
    if (!target) return;
    if (radius <= 0.0f) {
        target->StrokeRect(rect, color, lineWidth);
        return;
    }
    const float r =
        std::min(radius, std::min(rect.width, rect.height) * 0.5f);
    const float x0 = rect.x;
    const float y0 = rect.y;
    const float x1 = rect.x + rect.width;
    const float y1 = rect.y + rect.height;
    // The four corner centres and the angle each corner starts at. The
    // angles grow clockwise, because the y axis points downwards.
    const float centreX[4] = {x0 + r, x1 - r, x1 - r, x0 + r};
    const float centreY[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
    const float startAngle[4] = {180.0f, 270.0f, 0.0f, 90.0f};
    const int kSteps = 8;
    std::vector<Point> path;
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= kSteps; ++step) {
            const float angle =
                (startAngle[corner] + 90.0f * step / kSteps) * kPi / 180.0f;
            path.push_back(Point{centreX[corner] + r * std::cos(angle),
                                 centreY[corner] + r * std::sin(angle)});
        }
    }
    StrokePolygon(target, path, color, lineWidth);
}

// The scale of a diagram: the zoom, scaled down when the diagram is
// wider than the column it has to fit in.
float FitScale(float zoom, double naturalWidth, float availWidth) {
    float scale = zoom > 0.0f ? zoom : 1.0f;
    if (naturalWidth > 0 && availWidth > 0) {
        const float fit = availWidth / static_cast<float>(naturalWidth);
        if (fit < scale) scale = fit;
    }
    return scale;
}

// The font of the diagram text.
//
// The text has to shrink by the same factor the geometry shrank by, or
// the glyphs stay full size inside shrunken boxes.
FontSpec ScaledFont(const FontSpec& font, float scale) {
    FontSpec scaled = font;
    scaled.size = font.size * scale;
    return scaled;
}

// The font at an explicit mermaid size, keeping the face the caller
// names. Mermaid states the pie text in px (17 for the slices and the
// legend, 25 for the title) and the layout is in the same units, so the
// size comes from mermaid, not from a multiple of the body font.
FontSpec FontAtSize(const FontSpec& font, float size) {
    FontSpec sized = font;
    sized.size = size;
    return sized;
}

// Strokes a polyline with a dash pattern.
//
// The seam has no dash style, so the dashes are drawn as separate
// lines, walked along the polyline in screen points. The pattern counts
// in stroke widths, the way the Direct2D custom dash style did, and the
// phase carries across the joints so the dots keep their spacing.
void StrokeDashedPolyline(RenderTarget* target,
                          const std::vector<Point>& route, float strokeWidth,
                          const Color& color) {
    if (!target) return;
    const float dash = kFlowDash[0] * strokeWidth;
    const float gap = kFlowDash[1] * strokeWidth;
    float phase = 0.0f;
    bool on = true;
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const float x0 = route[i].x;
        const float y0 = route[i].y;
        const float x1 = route[i + 1].x;
        const float y1 = route[i + 1].y;
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f) continue;
        const float ux = dx / length;
        const float uy = dy / length;
        float travelled = 0.0f;
        while (travelled < length) {
            const float limit = on ? dash : gap;
            const float step = std::min(limit - phase, length - travelled);
            if (on && step > 0.0f) {
                target->StrokeLine(x0 + ux * travelled, y0 + uy * travelled,
                                   x0 + ux * (travelled + step),
                                   y0 + uy * (travelled + step), color,
                                   strokeWidth);
            }
            travelled += step;
            phase += step;
            if (phase >= limit - 0.001f) {
                phase = 0.0f;
                on = !on;
            }
        }
    }
}

// Draws the arrow head at the end of an edge. The head keeps its size on
// screen: its length and half width are divided by the scale, so a
// diagram shrunk into the column does not get a smaller arrow.
void DrawArrowHead(RenderTarget* target, const mermaid::Point& from,
                   const mermaid::Point& to, float ox, float oy, float scale,
                   const Color& color) {
    if (!target) return;
    double dx = to.x - from.x;
    double dy = to.y - from.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6) return;
    dx /= length;
    dy /= length;
    const float denom = scale > 1e-6f ? scale : 1.0f;
    const Point tip{ox + static_cast<float>(to.x * scale),
                    oy + static_cast<float>(to.y * scale)};
    // The base point in world units, so the head is the same size on
    // screen whatever the diagram was scaled to.
    const double baseX = to.x - dx * (kFlowArrowLength / denom);
    const double baseY = to.y - dy * (kFlowArrowLength / denom);
    const double px = -dy;
    const double py = dx;
    const double half = kFlowArrowHalfWidth / denom;
    const Point left{ox + static_cast<float>((baseX + px * half) * scale),
                     oy + static_cast<float>((baseY + py * half) * scale)};
    const Point right{ox + static_cast<float>((baseX - px * half) * scale),
                      oy + static_cast<float>((baseY - py * half) * scale)};
    target->FillPolygon({tip, left, right}, color);
}

// The widest line of a laid out label, which is the width an edge
// label's backing rect takes.
float LabelWidth(const TextLayout& block) {
    const std::vector<TextLine>* lines = block.Lines();
    if (!lines) return block.Width();
    float widest = 0.0f;
    for (const TextLine& line : *lines) {
        if (line.width > widest) widest = line.width;
    }
    return widest;
}

// Lays one flowchart label out through the caller's engine.
//
// `bodyBox` is the diagram at the page zoom: its labels wear the body
// line box then, so every line is lineBoxEm tall with the glyphs
// centred in it. A diagram the column shrank kept the font's natural
// metrics, the ones MeasureWrapped answers.
TextLayoutHandle LayoutFlowLabel(TextEngine* engine, const std::string& text,
                                 const FontSpec& font, float maxWidth,
                                 TextAlign align, bool bodyBox,
                                 float lineBoxEm) {
    GlyphRun run;
    run.text = text;
    run.font = font;
    std::vector<TextLine> lines = engine->MeasureWrapped({run}, maxWidth, 0.0f);
    if (bodyBox && !lines.empty()) {
        const float natural = lines.front().height;
        const float fontAscent = lines.front().ascent;
        const float height = lineBoxEm * font.size;
        const float ascent = (height - natural) * 0.5f + fontAscent;
        for (TextLine& line : lines) {
            line.height = height;
            line.ascent = ascent;
        }
    }
    return MakeLineLayout(std::move(lines), maxWidth, align);
}

// Paints a flowchart.
//
// The body is the one the Windows renderer carried until the flowchart
// moved onto this painter (plan 2.2, slice 6). The mac painter drew the
// same diagram with natural label boxes and a smaller arrow head; the
// two pictures disagreed, and this body is the reconciliation: one
// walk, one arrow head, one label rule, so both ports now draw the
// flowchart the Windows port always drew. `labelLineBoxEm` is the body
// line box the page-zoom labels wear; zero keeps the font's own box.
void PaintFlowchart(TextEngine* engine, RenderTarget* target,
                    const mermaid::LaidOutFlowchart& laid, float zoom,
                    float availWidth, float originX, float originY,
                    float padding, const Palette& palette,
                    const FontSpec& bodyFont, float labelLineBoxEm) {
    if (!engine || !target) return;
    const float scale = FitScale(zoom, laid.width, availWidth);
    const FontSpec font = ScaledFont(bodyFont, scale);
    const float ox = originX;
    const float oy = originY + padding;
    // Text shrinks by the same factor the geometry did. When it did not
    // shrink (the diagram is at the page zoom, or the fit was clamped),
    // the labels wear the body's uniform line box.
    const float textScale = scale / (zoom > 0.001f ? zoom : 1.0f);
    const bool bodyBox = labelLineBoxEm > 0.0f &&
                         !(textScale != 1.0f && textScale > 0.01f);

    const Color ink = ToColor(palette.mermaidEdge);
    const Color nodeFill = ToColor(palette.mermaidNodeFill);
    const Color nodeBorder = ToColor(palette.mermaidNodeBorder);
    const Color textColor = ToColor(palette.mermaidText);
    const Color laneFill = ToColor(palette.mermaidClusterBg);
    const Color laneBorder = ToColor(palette.mermaidClusterBorder);
    const Color laneTitle = ToColor(palette.textPrimary);
    const Color white{1.0f, 1.0f, 1.0f, 1.0f};

    // The swimlane bands come first, so the nodes and the edges are
    // drawn on top of them.
    for (const auto& lane : laid.lanes) {
        const float lx = ox + static_cast<float>(lane.x * scale);
        const float ly = oy + static_cast<float>(lane.y * scale);
        const float lw = static_cast<float>(lane.width * scale);
        const float lh = static_cast<float>(lane.height * scale);
        if (lw <= 0.0f || lh <= 0.0f) continue;
        const float radius = kFlowNodeRadius * scale;
        const Rect band{lx, ly, lw, lh};
        target->FillRoundedRect(band, radius, laneFill);
        target->StrokeRoundedRect(band, radius, laneBorder, 1.0f);

        if (!lane.title.empty()) {
            const TextLayoutHandle block = LayoutFlowLabel(
                engine, lane.title, font, lw, TextAlign::Left, bodyBox,
                labelLineBoxEm);
            const float titleX = lx + kFlowLaneInsetX * scale;
            const float titleY = ly + kFlowLaneInsetY * scale;
            target->PushClip(
                Rect{titleX, titleY, lw, kFlowLaneTitleHeight * scale});
            target->DrawLayout(*block, titleX, titleY, laneTitle);
            target->PopClip();
        }
    }

    // The edges.
    for (const auto& edge : laid.edges) {
        if (edge.route.size() < 2) continue;
        // A dotted edge is drawn with a dash pattern, and a thick edge
        // is drawn at a heavier weight, the way mermaid does it.
        const bool dotted = edge.style == mermaid::LineStyle::Dotted;
        const bool thick = edge.style == mermaid::LineStyle::Thick;
        const float strokeWidth =
            thick ? kFlowEdgeStrokeThick : kFlowEdgeStroke;
        std::vector<Point> route;
        route.reserve(edge.route.size());
        for (const mermaid::Point& point : edge.route) {
            route.push_back(
                Point{ox + static_cast<float>(point.x * scale),
                      oy + static_cast<float>(point.y * scale)});
        }
        if (dotted) {
            StrokeDashedPolyline(target, route, strokeWidth, ink);
        } else {
            target->StrokePolygon(route, ink, strokeWidth, false);
        }
        if (edge.head == mermaid::Head::Arrow) {
            DrawArrowHead(target, edge.route[edge.route.size() - 2],
                          edge.route.back(), ox, oy, scale, ink);
        }

        // The edge label sits on the middle of the route, on an opaque
        // backing, so the line does not strike through the glyphs.
        if (!edge.label.empty()) {
            const size_t middle = (edge.route.size() - 1) / 2;
            const double mx =
                (edge.route[middle].x + edge.route[middle + 1].x) * 0.5;
            const double my =
                (edge.route[middle].y + edge.route[middle + 1].y) * 0.5;
            const float maxLabelWidth = kFlowEdgeLabelWidth * scale;
            if (!(maxLabelWidth > 0.0f)) continue;
            const TextLayoutHandle block = LayoutFlowLabel(
                engine, edge.label, font, maxLabelWidth, TextAlign::Left,
                bodyBox, labelLineBoxEm);
            const float labelWidth = LabelWidth(*block);
            const float labelHeight = block->Height();
            const float lx = ox + static_cast<float>(mx * scale) -
                             labelWidth * 0.5f;
            const float ly = oy + static_cast<float>(my * scale) -
                             labelHeight * 0.5f;
            target->FillRect(Rect{lx - kFlowEdgeLabelPad, ly - kFlowEdgeLabelPad,
                                  labelWidth + 2.0f * kFlowEdgeLabelPad,
                                  labelHeight + 2.0f * kFlowEdgeLabelPad},
                             white);
            target->PushClip(Rect{lx, ly, maxLabelWidth,
                                  kFlowEdgeLabelHeight * scale});
            target->DrawLayout(*block, lx, ly, textColor);
            target->PopClip();
        }
    }

    // The nodes.
    for (const auto& node : laid.nodes) {
        if (node.is_dummy) continue;
        const float nx =
            ox + static_cast<float>((node.x - node.width * 0.5f) * scale);
        const float ny =
            oy + static_cast<float>((node.y - node.height * 0.5f) * scale);
        const float nw = static_cast<float>(node.width * scale);
        const float nh = static_cast<float>(node.height * scale);
        Rect box;
        box.x = nx;
        box.y = ny;
        box.width = nw;
        box.height = nh;

        switch (node.shape) {
            case mermaid::NodeShape::Round: {
                const float radius = kFlowNodeRadius * scale;
                target->FillRoundedRect(box, radius, nodeFill);
                target->StrokeRoundedRect(box, radius, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Stadium: {
                const float radius = nh * 0.5f;
                target->FillRoundedRect(box, radius, nodeFill);
                target->StrokeRoundedRect(box, radius, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Circle: {
                target->FillEllipse(box, nodeFill);
                target->StrokeEllipse(box, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Diamond: {
                const std::vector<Point> corners = {
                    Point{nx + nw * 0.5f, ny},
                    Point{nx + nw, ny + nh * 0.5f},
                    Point{nx + nw * 0.5f, ny + nh},
                    Point{nx, ny + nh * 0.5f}};
                target->FillPolygon(corners, nodeFill);
                target->StrokePolygon(corners, nodeBorder, 1.0f, true);
                break;
            }
            case mermaid::NodeShape::Rect:
            default: {
                target->FillRect(box, nodeFill);
                target->StrokeRect(box, nodeBorder, 1.0f);
                break;
            }
        }

        // The label, centred in the box. Mermaid's line-break
        // separators (<br/> and friends) split the label into lines,
        // which the engine lays out as one block the box centres.
        if (node.label.empty() || !(nw > 0.0f) || !(nh > 0.0f)) continue;
        std::vector<std::string> lines = mermaid::SplitLabelLines(node.label);
        if (lines.empty()) lines.push_back(node.label);
        std::string joined;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) joined.push_back('\n');
            joined += lines[i];
        }
        const TextLayoutHandle block =
            LayoutFlowLabel(engine, joined, font, nw, TextAlign::Center,
                            bodyBox, labelLineBoxEm);
        const float labelHeight = block->Height();
        target->PushClip(box);
        target->DrawLayout(*block, nx, ny + (nh - labelHeight) * 0.5f,
                           textColor);
        target->PopClip();
    }
}


// Draws the pie's outline once: the ring around the slices and one
// radial divider per boundary angle.
//
// Mermaid strokes every slice path, so a radius shared by two slices is
// stroked twice and the ring is stroked once per slice on top of
// pieOuterCircle. The same edge painted twice is what made the border
// read as a heavy black outline and doubled every divider. Here the ring
// is stroked once and every boundary angle once: each arc's start is a
// boundary, and the last arc's end is the first arc's start, so the arcs
// cover all boundaries with none drawn twice. The angles are the layout
// ones: clockwise from twelve o'clock.
void StrokePieOutline(RenderTarget* target, float centreX, float centreY,
                      float radius, float ringRadius,
                      const std::vector<mermaid::PieArc>& arcs,
                      const Color& ink, float lineWidth) {
    if (!target) return;
    Rect ring;
    ring.x = centreX - ringRadius;
    ring.y = centreY - ringRadius;
    ring.width = ringRadius * 2.0f;
    ring.height = ringRadius * 2.0f;
    target->StrokeEllipse(ring, ink, lineWidth);

    for (size_t i = 0; i < arcs.size(); ++i) {
        // The boundary at this arc's start is shared with the arc before
        // it, so the ink is chosen against both fills.
        const auto& arc = arcs[i];
        const int before = arcs[(i + arcs.size() - 1) % arcs.size()].color_index;
        const Color inkAt =
            DividerInk(Rgb(kPieColors[before % kPieColorCount]),
                       Rgb(kPieColors[arc.color_index % kPieColorCount]));
        target->StrokeLine(
            centreX, centreY,
            centreX + radius * static_cast<float>(std::sin(arc.start_angle)),
            centreY - radius * static_cast<float>(std::cos(arc.start_angle)),
            inkAt, lineWidth);
    }
}

// Paints a pie chart.
void PaintPie(TextEngine* engine, RenderTarget* target,
              const mermaid::LaidOutPie& laid, float zoom, float availWidth,
              float originX, float originY, float padding,
              const Palette& palette, const FontSpec& bodyFont) {
    const float scale = FitScale(zoom, laid.width, availWidth);
    const float ox = originX;
    const float oy = originY + padding;

    // The mermaid ink of the page: the ring, the dividers, the legend
    // and the title all sit on the page or on its edge, so they follow
    // the appearance. The percent labels sit on the slices, which keep
    // mermaid's pale palette in both appearances, so their ink is chosen
    // against the slice instead.
    const Color pageInk = ToColor(palette.mermaidText);

    // Mermaid's own pie text sizes. The document body font is larger and
    // scaled the percents and the legend up with the page text.
    const FontSpec sectionFont =
        FontAtSize(bodyFont, kPieSectionFontSize * scale);
    const FontSpec legendFont =
        FontAtSize(bodyFont, kPieLegendFontSize * scale);
    const FontSpec titleFont = FontAtSize(bodyFont, kPieTitleFontSize * scale);

    const float centreX = ox + static_cast<float>(laid.cx * scale);
    const float centreY = oy + static_cast<float>(laid.cy * scale);
    const float radius = static_cast<float>(laid.radius * scale);
    const float ringRadius = static_cast<float>(laid.ring_r * scale);

    // The slices, filled only: the outline is painted once below, not
    // once per slice.
    for (const auto& arc : laid.arcs) {
        const Color fill = Rgb(kPieColors[arc.color_index % kPieColorCount]);
        Rect slice;
        slice.x = centreX - radius;
        slice.y = centreY - radius;
        slice.width = radius * 2.0f;
        slice.height = radius * 2.0f;
        // The seam counts the angles clockwise from three o'clock and
        // the layout counts them clockwise from twelve o'clock, so the
        // layout angle is turned by a quarter turn.
        const float start =
            static_cast<float>(arc.start_angle * 180.0 / kPi) - 90.0f;
        const float end =
            static_cast<float>(arc.end_angle * 180.0 / kPi) - 90.0f;
        target->FillPieSlice(slice, start, end, fill);
    }

    StrokePieOutline(target, centreX, centreY, radius, ringRadius, laid.arcs,
                     pageInk, kPieStrokeWidth * scale);

    // The percent label of every slice, on the angle that halves the
    // slice. The position comes from PlacePieLabel, which keeps the whole
    // box inside the wedge with a margin (see pie_layout.h); a slice too
    // narrow for its text shrinks the type, and one too narrow for that
    // is reported here and drawn at the rim on its bisector.
    for (const auto& arc : laid.arcs) {
        const Color slice = Rgb(kPieColors[arc.color_index % kPieColorCount]);
        // Rec. 601 luma: pale slices take the dark ink, saturated ones
        // the light one, so a 20% label is readable on either.
        const float luma =
            0.299f * slice.r + 0.587f * slice.g + 0.114f * slice.b;
        const Color labelInk = luma > 0.62f ? Rgb(0x333333) : Rgb(0xF5F5F5);
        const TextSize box = MeasureText(engine, arc.pct, sectionFont);
        const mermaid::PieLabelPlacement place = mermaid::PlacePieLabel(
            arc, box.width * 0.5, box.height * 0.5, radius);
        if (!place.inside) {
            std::fprintf(stderr,
                         "mermaid pie: label %s does not fit its %.1f degree "
                         "slice at any radius\n",
                         arc.pct.c_str(),
                         (arc.end_angle - arc.start_angle) * 180.0 / kPi);
        }
        const FontSpec labelFont =
            place.font_scale == 1.0
                ? sectionFont
                : FontAtSize(bodyFont, kPieSectionFontSize * scale *
                                           static_cast<float>(place.font_scale));
        DrawCentered(target, engine, arc.pct, labelFont,
                     centreX + static_cast<float>(place.x),
                     centreY + static_cast<float>(place.y), labelInk);
    }

    // The title sits centred above the pie at mermaid's own offset,
    // -(height - 50) / 2, which is 200 units above the centre.
    if (!laid.title.empty()) {
        DrawCentered(target, engine, laid.title, titleFont, centreX,
                     centreY - 200.0f * scale, pageInk);
    }

    // The legend sits beside the pie, vertically centred on it. Mermaid
    // put both in one group translated to (cx, cy), so the layout's row
    // x and y are relative to the centre; drawing them from the block
    // origin put the block above the circle and on top of it.
    if (laid.legend.empty()) return;

    const TextSize legendLine = MeasureText(engine, "Ag", legendFont);
    // The pitch follows the measured line height so a taller engine line
    // cannot make two entries overlap; the swatch is sized to the font.
    const float legendPitch =
        std::max(kPieLegendPitch * scale,
                 legendLine.height + kPieLegendGap * scale);
    const float swatchSize = legendFont.size;
    const float legendWidthExtra = swatchSize + kPieLegendGap * scale;
    const int legendCount = static_cast<int>(laid.legend.size());

    float legendTextWidth = 0.0f;
    for (const auto& row : laid.legend) {
        legendTextWidth = std::max(
            legendTextWidth, MeasureText(engine, row.label, legendFont).width);
    }

    const float bandTop = oy;
    const float bandBottom = oy + static_cast<float>(laid.height * scale);
    const float bandRight = ox + static_cast<float>(laid.width * scale);
    const float legendLeftBase =
        ox + static_cast<float>((laid.cx + laid.legend.front().x) * scale);
    float legendLeft = legendLeftBase;
    float legendTop = centreY - legendPitch * legendCount * 0.5f;
    const float legendWidth = legendTextWidth + legendWidthExtra;
    const float legendHeight = legendPitch * legendCount;

    // Hold the block inside the band the layout pass allocated. The
    // layout width already reserves the legend column; if the measured
    // text is wider than that estimate, or a long legend is taller than
    // the canvas, the block is kept inside the band and the shortfall is
    // reported so the layout band can grow. The painter never draws over
    // the block edge.
    if (legendHeight > bandBottom - bandTop) {
        std::fprintf(stderr,
                     "mermaid pie: legend needs %.1f DIP, layout band has "
                     "%.1f; the layout band has to grow\n",
                     legendHeight, bandBottom - bandTop);
    }
    if (legendTop < bandTop) legendTop = bandTop;
    if (legendTop + legendHeight > bandBottom) {
        legendTop = std::max(bandTop, bandBottom - legendHeight);
    }
    if (legendLeft + legendWidth > bandRight) {
        const float shortfall = legendLeft + legendWidth - bandRight;
        std::fprintf(stderr,
                     "mermaid pie: legend needs %.1f DIP more width; the "
                     "layout band has to grow\n",
                     shortfall);
        legendLeft = std::max(ox, bandRight - legendWidth);
    }

    for (size_t i = 0; i < laid.legend.size(); ++i) {
        const auto& row = laid.legend[i];
        const float rowMid = legendTop + legendPitch *
                                            (static_cast<float>(i) + 0.5f);
        Rect swatch;
        swatch.x = legendLeft;
        swatch.y = rowMid - swatchSize * 0.5f;
        swatch.width = swatchSize;
        swatch.height = swatchSize;
        const Color fill = Rgb(kPieColors[row.color_index % kPieColorCount]);
        target->FillRect(swatch, fill);
        // A hairline on every swatch. Three of the pastel fills sit on a
        // page that is nearly the same value, and without an edge the
        // key's first column read as blank gaps rather than as colours.
        // The ink is the divider rule's: the dark or the light line,
        // whichever separates the fill from the page better.
        target->StrokeRect(swatch, DividerInk(fill, ToColor(palette.pageBg)),
                           1.0f);
        DrawLeftCentered(target, engine, row.label, legendFont,
                         legendLeft + legendWidthExtra, rowMid, pageInk);
    }
}

// Parses a CSS colour (rgb(r,g,b) or #rrggbb) into a 0xRRGGBB value.
//
// The alpha is ignored, because the reference diagrams are opaque.
bool ParseCssColor(const std::string& css, uint32_t* out) {
    if (!out || css.empty()) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto hex2 = [&](const std::string& text, size_t i) -> int {
        if (i + 1 >= text.size()) return -1;
        return nib(text[i]) * 16 + nib(text[i + 1]);
    };
    if (css[0] == '#' && (css.size() == 7 || css.size() == 9)) {
        const int r = hex2(css, 1);
        const int g = hex2(css, 3);
        const int b = hex2(css, 5);
        if (r < 0 || g < 0 || b < 0) return false;
        *out = static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 |
               static_cast<uint32_t>(b);
        return true;
    }
    int r = 0;
    int g = 0;
    int b = 0;
    if (sscanf(css.c_str(), "rgb(%d, %d, %d)", &r, &g, &b) == 3 ||
        sscanf(css.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3) {
        *out = static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 |
               static_cast<uint32_t>(b);
        return true;
    }
    return false;
}

// Draws a cubic Bezier as a chain of short lines.
//
// The seam has no curves, so the self message path is flattened. The
// curve is short, so a dozen segments are enough for it to look smooth.
void StrokeBezier(RenderTarget* target, float x0, float y0, float x1,
                  float y1, float x2, float y2, float x3, float y3,
                  const Color& color, float lineWidth) {
    if (!target) return;
    const int kSteps = 16;
    float px = x0;
    float py = y0;
    for (int i = 1; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float u = 1.0f - t;
        const float bx = u * u * u * x0 + 3.0f * u * u * t * x1 +
                         3.0f * u * t * t * x2 + t * t * t * x3;
        const float by = u * u * u * y0 + 3.0f * u * u * t * y1 +
                         3.0f * u * t * t * y2 + t * t * t * y3;
        target->StrokeLine(px, py, bx, by, color, lineWidth);
        px = bx;
        py = by;
    }
}

// Draws the mermaid stickman actor.
//
// The proportions follow the mermaid actor-man svg: a 50 by 65 unit box
// with the head in the top middle.
void DrawStickman(RenderTarget* target, const Color& stroke,
                  const Color& headFill, float cx, float topY, float unit) {
    if (!target) return;
    auto X = [&](float fx) { return cx + (fx - 25.0f) * unit; };
    auto Y = [&](float fy) { return topY + fy * unit; };
    // The head.
    Rect head;
    head.x = X(25.0f) - 6.25f * unit;
    head.y = Y(6.25f) - 6.25f * unit;
    head.width = 12.5f * unit;
    head.height = 12.5f * unit;
    target->FillEllipse(head, headFill);
    target->StrokeEllipse(head, stroke, 1.5f * unit);
    // The body.
    target->StrokeLine(X(25.0f), Y(12.5f), X(25.0f), Y(25.0f), stroke,
                        1.5f * unit);
    // The arms.
    target->StrokeLine(X(10.0f), Y(16.0f), X(40.0f), Y(16.0f), stroke,
                        1.5f * unit);
    // The legs.
    target->StrokeLine(X(25.0f), Y(25.0f), X(18.0f), Y(40.0f), stroke,
                        1.5f * unit);
    target->StrokeLine(X(25.0f), Y(25.0f), X(32.0f), Y(40.0f), stroke,
                        1.5f * unit);
}

// Paints a sequence diagram.
void PaintSequence(TextEngine* engine, RenderTarget* target,
                   const mermaid::LaidOutSequence& laid, float zoom,
                   float availWidth, float originX, float originY,
                   float padding, const Palette& palette,
                   const FontSpec& bodyFont) {
    const float scale = FitScale(zoom, laid.width, availWidth);
    const FontSpec font = ScaledFont(bodyFont, scale);
    FontSpec numberFont = bodyFont;
    numberFont.size = kNumberFontSize * scale;

    // The layout lives in mermaid's viewBox space, which can start left
    // of and above the origin, so the origin moves with it.
    const float ox = originX - static_cast<float>(laid.startx * scale);
    const float oy =
        originY + padding - static_cast<float>(laid.starty * scale);
    auto X = [&](double v) { return ox + static_cast<float>(v * scale); };
    auto Y = [&](double v) { return oy + static_cast<float>(v * scale); };

    const Color ink = ToColor(palette.mermaidText);
    const Color actorFill = ToColor(palette.mermaidActorFill);
    const Color actorStroke = ToColor(palette.mermaidActorStroke);
    const Color noteFill = ToColor(palette.mermaidNoteFill);
    const Color noteStroke = ToColor(palette.mermaidNoteStroke);
    const Color lifeline = ToColor(palette.mermaidLifeline);
    const Color activation = ToColor(palette.mermaidActivationFill);
    const Color activationStroke = ToColor(palette.mermaidActivationStroke);
    const Color white = Rgb(0xFFFFFF);

    // The background of a rect block. Mermaid puts one fill per block,
    // and the layout keeps them in order.
    for (const auto& background : laid.backgrounds) {
        Color fill = ToColor(palette.mermaidLoopFill);
        uint32_t rgb = 0;
        if (ParseCssColor(background.fill, &rgb)) fill = Rgb(rgb);
        Rect rect;
        rect.x = X(background.x);
        rect.y = Y(background.y);
        rect.width = static_cast<float>(background.w * scale);
        rect.height = static_cast<float>(background.h * scale);
        target->FillRect(rect, fill);
    }

    // The loop, alt, par, opt, critical and break frames. A rect loop
    // is only a background, so it has no frame and no label.
    for (const auto& loop : laid.loops) {
        if (loop.kind == "rect") continue;
        Rect frame;
        frame.x = X(loop.startx);
        frame.y = Y(loop.starty);
        frame.width = static_cast<float>((loop.stopx - loop.startx) * scale);
        frame.height = static_cast<float>((loop.stopy - loop.starty) * scale);
        target->StrokeRect(frame, ink, 1.0f * scale);

        // The label box: a filled tag with the keyword of the frame.
        if (!loop.label.empty()) {
            const float tagWidth = 60.0f * scale;
            const float tagHeight = 20.0f * scale;
            Rect tag;
            tag.x = X(loop.startx);
            tag.y = Y(loop.starty);
            tag.width = tagWidth;
            tag.height = tagHeight;
            target->FillRect(tag, actorFill);
            target->StrokeRect(tag, ink, 1.0f * scale);
            DrawCentered(target, engine, loop.label, font,
                         tag.x + tagWidth * 0.5f, tag.y + tagHeight * 0.5f,
                         ink);
        }
        if (!loop.title.empty()) {
            DrawLeftCentered(target, engine, loop.title, font,
                             X(loop.startx) + 70.0f * scale,
                             Y(loop.starty) + 10.0f * scale, ink);
        }

        // The section dividers. Every branch gets a tag like the frame
        // keyword box, and the title of the branch goes to the right of
        // it.
        for (size_t i = 0; i < loop.section_y.size(); ++i) {
            const float dividerY = Y(loop.section_y[i]);
            target->StrokeLine(X(loop.startx), dividerY, X(loop.stopx),
                               dividerY, ink, 1.0f * scale);
            const char* keyword = loop.kind == "par" ? "and"
                                  : loop.kind == "critical" ? "option"
                                  : "else";
            const float tagWidth = 60.0f * scale;
            const float tagHeight = 20.0f * scale;
            Rect tag;
            tag.x = X(loop.startx);
            tag.y = dividerY;
            tag.width = tagWidth;
            tag.height = tagHeight;
            target->FillRect(tag, actorFill);
            target->StrokeRect(tag, ink, 1.0f * scale);
            DrawCentered(target, engine, keyword, font,
                         tag.x + tagWidth * 0.5f, dividerY + tagHeight * 0.5f,
                         ink);
            if (i < loop.section_titles.size() &&
                !loop.section_titles[i].empty()) {
                DrawLeftCentered(target, engine, loop.section_titles[i], font,
                                 X(loop.startx) + 70.0f * scale,
                                 dividerY + tagHeight * 0.5f, ink);
            }
        }
    }

    // The lifelines, from the bottom of the actor box to the top of the
    // footer box.
    for (const auto& actor : laid.actors) {
        const float centre = X(actor.lifeline_x);
        const float boxBottom = Y(0.0) + kActorBoxHeight * scale;
        const float footerTop = std::max(Y(actor.stopy), boxBottom);
        target->StrokeLine(centre, boxBottom, centre, footerTop, lifeline,
                           1.0f * scale);
    }

    // The activations.
    for (const auto& act : laid.activations) {
        Rect rect;
        rect.x = X(act.x);
        rect.y = Y(act.y);
        rect.width = static_cast<float>(act.w * scale);
        rect.height = static_cast<float>(act.h * scale);
        target->FillRect(rect, activation);
        target->StrokeRect(rect, activationStroke, 1.0f * scale);
    }

    // The messages.
    for (const auto& message : laid.messages) {
        const bool dotted = message.type == mermaid::MsgType::Dotted;
        if (message.self) {
            // The self path: a curve out to the right and back to the
            // lifeline.
            const float sx = X(message.x1);
            const float sy = Y(message.y1);
            const float ex = X(message.x2);
            const float ey = Y(message.y2);
            StrokeBezier(target, sx, sy, sx + 60.0f * scale,
                         sy - 10.0f * scale, sx + 60.0f * scale,
                         sy + 30.0f * scale, ex, ey, ink, 1.5f * scale);
            // The head points back at the lifeline.
            const std::vector<Point> head = {
                Point{ex, ey},
                Point{ex + 10.0f * scale, ey - 4.0f * scale},
                Point{ex + 10.0f * scale, ey + 4.0f * scale}};
            target->FillPolygon(head, ink);
        } else {
            const float x1 = X(message.x1);
            const float y1 = Y(message.y1);
            const float x2 = X(message.x2);
            const float y2 = Y(message.y2);
            const float dir = (x2 >= x1) ? 1.0f : -1.0f;
            if (dotted) {
                // The dashed line is drawn as short segments.
                const float dx = x2 - x1;
                const float dy = y2 - y1;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.5f) {
                    const float ux = dx / length;
                    const float uy = dy / length;
                    const float segment = 5.0f * scale;
                    const float gap = 4.0f * scale;
                    float travelled = 0.0f;
                    while (travelled < length) {
                        const float end = std::min(travelled + segment, length);
                        target->StrokeLine(x1 + ux * travelled, y1 + uy * travelled,
                                           x1 + ux * end, y1 + uy * end, ink,
                                           1.5f * scale);
                        travelled += segment + gap;
                    }
                }
                // The open head of a dotted message.
                target->StrokeLine(x2, y2, x2 - 10.0f * dir * scale,
                                   y2 - 5.0f * scale, ink, 1.5f * scale);
                target->StrokeLine(x2, y2, x2 - 10.0f * dir * scale,
                                   y2 + 5.0f * scale, ink, 1.5f * scale);
            } else {
                target->StrokeLine(x1, y1, x2, y2, ink, 1.5f * scale);
                const std::vector<Point> head = {
                    Point{x2, y2},
                    Point{x2 - 11.0f * dir * scale, y2 - 5.0f * scale},
                    Point{x2 - 11.0f * dir * scale, y2 + 5.0f * scale}};
                target->FillPolygon(head, ink);
            }
        }
        if (!message.text.empty()) {
            // The text of a message sits above the line, on the
            // baseline the layout gives it.
            DrawBaseline(target, engine, message.text, font, X(message.tx),
                         Y(message.ty) + 25.0f * scale, ink);
        }
    }

    // The autonumber discs. Mermaid draws the marker with a stroke
    // width of two, so the disc is twice the radius of the marker.
    for (const auto& number : laid.numbers) {
        const float centreX = X(number.x);
        const float centreY = Y(number.y);
        const float radius = 12.0f * scale;
        Rect disc;
        disc.x = centreX - radius;
        disc.y = centreY - radius;
        disc.width = radius * 2.0f;
        disc.height = radius * 2.0f;
        target->FillEllipse(disc, ink);
        DrawBaseline(target, engine, std::to_string(number.n), numberFont,
                     centreX, centreY + 4.0f * scale, white);
    }

    // The notes.
    for (const auto& note : laid.notes) {
        Rect rect;
        rect.x = X(note.x);
        rect.y = Y(note.y);
        rect.width = static_cast<float>(note.w * scale);
        rect.height = static_cast<float>(note.h * scale);
        const float radius = 4.0f * scale;
        target->FillRoundedRect(rect, radius, noteFill);
        StrokeRoundedRect(target, rect, radius, noteStroke, 1.0f * scale);
        DrawBaseline(target, engine, note.text, font, X(note.tx),
                     Y(note.ty) + 17.0f * scale, ink);
    }

    // The actor boxes, and the footers that mirror them.
    for (const auto& actor : laid.actors) {
        const bool man = actor.shape == mermaid::ActorShape::ActorStickman;
        const float left = X(actor.x);
        const float width = static_cast<float>(actor.w * scale);
        const float boxHeight = kActorBoxHeight * scale;
        const float footerY = Y(actor.stopy);

        for (int part = 0; part < 2; ++part) {
            const float top = part == 0 ? Y(0.0) : footerY;
            if (man) {
                DrawStickman(target, actorStroke, actorFill,
                             left + width * 0.5f, top, scale);
                DrawCenteredTop(target, engine, actor.name, font,
                                left + width * 0.5f, top + boxHeight * 0.9f,
                                ink);
            } else {
                Rect box;
                box.x = left;
                box.y = top;
                box.width = width;
                box.height = boxHeight;
                target->FillRect(box, actorFill);
                target->StrokeRect(box, actorStroke, 1.0f * scale);
                DrawCentered(target, engine, actor.name, font,
                             left + width * 0.5f, top + boxHeight * 0.5f, ink);
            }
        }
    }

    // The title, which the layout places above the diagram.
    if (!laid.title.empty()) {
        DrawCentered(target, engine, laid.title, font, X(laid.title_x), Y(-25.0),
                     ink);
    }
}

// Paints a gantt chart.
//
// Rows stack top to bottom: a section header row (its title, no bar),
// then that section's task rows. A plain task draws as a rounded bar; a
// milestone as a diamond; "crit" tasks get the mermaid crit red, "done"
// a muted fill, everything else the default teal.
void PaintGantt(TextEngine* engine, RenderTarget* target,
                const mermaid::LaidOutGantt& laid, float zoom,
                float availWidth, float originX, float originY,
                float padding, const FontSpec& bodyFont) {
    const float scale = FitScale(zoom, laid.width, availWidth);
    const FontSpec font = ScaledFont(bodyFont, scale);
    FontSpec titleFont = bodyFont;
    titleFont.size = bodyFont.size * 1.1f * scale;
    FontSpec sectionFont = font;
    sectionFont.bold = true;

    const float ox = originX;
    const float oy = originY + padding;
    auto X = [&](double v) { return ox + static_cast<float>(v * scale); };
    auto Y = [&](double v) { return oy + static_cast<float>(v * scale); };

    const Color ink = Rgb(0x333333);
    const Color gridLine = Rgb(0xE0E0E0);
    const Color bandFill = Rgb(0xF2F2F2);
    const Color barDefault = Rgb(0x4C9AAE);
    const Color barCrit = Rgb(0xB33A3A);
    const Color barDone = Rgb(0x9AA5AD);
    const Color barActive = Rgb(0x6FB7CC);
    const Color milestoneFill = Rgb(0x333333);

    if (!laid.title.empty()) {
        DrawLeftCentered(target, engine, laid.title, titleFont, X(laid.title_x),
                         Y(laid.title_y), ink);
    }

    // Vertical grid lines, one per axis tick, spanning the chart body.
    for (const auto& tick : laid.ticks) {
        target->StrokeLine(X(tick.x), Y(laid.grid_top), X(tick.x),
                           Y(laid.grid_bottom), gridLine, 1.0f * scale);
        DrawCentered(target, engine, tick.label, font, X(tick.x),
                     Y(laid.grid_bottom) + 10.0f * scale, ink);
    }

    // Section bands: an alternating pale background and the section
    // title at the start of its header row.
    for (const auto& band : laid.sections) {
        if (band.color_index % 2 == 1) {
            Rect rect;
            rect.x = X(0.0);
            rect.y = Y(band.y);
            rect.width = static_cast<float>(laid.width * scale);
            rect.height = static_cast<float>(band.height * scale);
            target->FillRect(rect, bandFill);
        }
        if (!band.title.empty()) {
            DrawLeftCentered(target, engine, band.title, sectionFont,
                             X(band.title_x), Y(band.title_y), ink);
        }
    }

    // The bars and milestone diamonds.
    for (const auto& bar : laid.bars) {
        Color fill = barDefault;
        if (bar.crit) fill = barCrit;
        else if (bar.done) fill = barDone;
        else if (bar.active) fill = barActive;

        if (bar.milestone) {
            const float side = static_cast<float>(bar.height * scale);
            const float cx = X(bar.x) + side * 0.5f;
            const float cy = Y(bar.y) + side * 0.5f;
            const float half = side * 0.5f;
            const std::vector<Point> diamond = {
                Point{cx, cy - half}, Point{cx + half, cy},
                Point{cx, cy + half}, Point{cx - half, cy}};
            target->FillPolygon(diamond, milestoneFill);
        } else {
            Rect rect;
            rect.x = X(bar.x);
            rect.y = Y(bar.y);
            rect.width = static_cast<float>(bar.width * scale);
            rect.height = static_cast<float>(bar.height * scale);
            target->FillRoundedRect(rect, 3.0f * scale, fill);
        }
        if (!bar.label.empty()) {
            DrawLeftCentered(target, engine, bar.label, font, X(bar.label_x),
                             Y(bar.label_y), ink);
        }
    }
}

}  // namespace

// A state diagram: circles for [*], a box per state, a polyline per
// transition, the geometry the shared state layout produced.
void PaintState(TextEngine* engine, RenderTarget* target,
                const mermaid::LaidOutState& laid, float zoom,
                float availWidth, float originX, float originY, float padding,
                const Palette& palette, const FontSpec& bodyFont) {
    const double natural = laid.width;
    const float scale = FitScale(
        zoom, static_cast<float>(natural > 0 ? natural : 0.0), availWidth);
    const FontSpec font = ScaledFont(bodyFont, scale);

    const float ox = originX;
    const float oy = originY + padding;
    auto X = [&](double v) { return ox + static_cast<float>(v * scale); };
    auto Y = [&](double v) { return oy + static_cast<float>(v * scale); };

    const Color fill = ToColor(palette.mermaidNodeFill);
    const Color border = ToColor(palette.mermaidNodeBorder);
    const Color edge = ToColor(palette.mermaidEdge);
    const Color ink = ToColor(palette.mermaidText);

    // Transitions under the boxes, the order mermaid draws in.
    for (const auto& e : laid.edges) {
        for (size_t i = 1; i < e.points.size(); ++i) {
            target->StrokeLine(X(e.points[i - 1].x), Y(e.points[i - 1].y),
                               X(e.points[i].x), Y(e.points[i].y), edge,
                               1.5f * scale);
        }
        if (e.points.size() >= 2) {
            const auto& a = e.points[e.points.size() - 2];
            const auto& b = e.points[e.points.size() - 1];
            const float x1 = X(a.x), y1 = Y(a.y), x2 = X(b.x), y2 = Y(b.y);
            const float dx = x2 - x1, dy = y2 - y1;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.001f) {
                const float ux = dx / len, uy = dy / len;
                const float size = 7.0f * scale;
                const float bx = x2 - ux * size, by = y2 - uy * size;
                target->StrokeLine(x2, y2, bx + uy * size * 0.5f,
                                   by - ux * size * 0.5f, edge, 1.0f * scale);
                target->StrokeLine(x2, y2, bx - uy * size * 0.5f,
                                   by + ux * size * 0.5f, edge, 1.0f * scale);
            }
        }
    }

    for (const auto& node : laid.nodes) {
        if (node.kind == "start" || node.kind == "end") {
            const float r = static_cast<float>(node.r * scale);
            Rect circle{X(node.cx) - r, Y(node.cy) - r, r * 2.0f, r * 2.0f};
            target->FillEllipse(circle, border);
            if (node.kind == "end") {
                const float inner = r * 0.62f;
                Rect ring{X(node.cx) - inner, Y(node.cy) - inner, inner * 2.0f,
                          inner * 2.0f};
                target->FillEllipse(ring, fill);
            }
            continue;
        }
        Rect box{X(node.cx - node.w * 0.5), Y(node.cy - node.h * 0.5),
                 static_cast<float>(node.w * scale),
                 static_cast<float>(node.h * scale)};
        target->FillRect(box, fill);
        target->StrokeRect(box, border, 1.0f * scale);
        DrawCentered(target, engine, node.text, font, X(node.cx), Y(node.cy),
                     ink);
    }

    // The layout gives the label's top-left corner; the painter centres
    // its text, so the centre is half the box further on.
    for (const auto& label : laid.labels) {
        DrawCentered(target, engine, label.text, font,
                     X(label.x + label.w * 0.5), Y(label.y + label.h * 0.5),
                     ink);
    }
}

void PaintMermaidDiagram(TextEngine* engine, RenderTarget* target,
                          const mermaid::MermaidRender& diagram, float zoom,
                          float availWidth, float originX, float originY,
                          float padding, const Palette& palette,
                          const FontSpec& bodyFont, float labelLineBoxEm) {
    if (!target) return;
    switch (diagram.kind) {
        case mermaid::MermaidKind::Pie:
            if (diagram.pie) {
                PaintPie(engine, target, *diagram.pie, zoom, availWidth,
                         originX, originY, padding, palette, bodyFont);
            }
            break;
        case mermaid::MermaidKind::Sequence:
            if (diagram.seq) {
                PaintSequence(engine, target, *diagram.seq, zoom, availWidth,
                              originX, originY, padding, palette, bodyFont);
            }
            break;
        case mermaid::MermaidKind::Gantt:
            if (diagram.gantt) {
                PaintGantt(engine, target, *diagram.gantt, zoom, availWidth,
                           originX, originY, padding, bodyFont);
            }
            break;
        case mermaid::MermaidKind::State:
            if (diagram.state) {
                PaintState(engine, target, *diagram.state, zoom, availWidth,
                           originX, originY, padding, palette, bodyFont);
            }
            break;
        case mermaid::MermaidKind::Flowchart:
        default:
            if (diagram.flow) {
                PaintFlowchart(engine, target, *diagram.flow, zoom, availWidth,
                               originX, originY, padding, palette, bodyFont,
                               labelLineBoxEm);
            }
            break;
    }
}

}  // namespace platform
