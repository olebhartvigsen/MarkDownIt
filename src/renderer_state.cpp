// The state diagram painter (Windows).
//
// The layout is mermaid's own numbers, taken from the shared state
// layout: circles for [*], a box for a state, a polyline for a
// transition. The painter owns nothing but the brushes, so both ports
// draw the same geometry with their own stack.

#include "renderer.h"

#include <cmath>
#include <string>

namespace {

// A centred run of text at a point in points.
void DrawStateText(IDWriteFactory* dw, ID2D1RenderTarget* rt,
                   IDWriteTextFormat* format, const std::string& text,
                   float cx, float cy, float scale, ID2D1Brush* brush) {
    if (text.empty() || !format) return;
    std::wstring wide(text.begin(), text.end());
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(dw->CreateTextLayout(wide.c_str(),
                                    static_cast<UINT32>(wide.size()), format,
                                    4096.0f, 4096.0f, &layout)) ||
        !layout) {
        return;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float baseline = static_cast<float>(metrics.height);
    rt->DrawTextLayout(
        D2D1::Point2F(cx - metrics.width * 0.5f, cy - baseline * 0.5f), layout,
        brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

// The arrowhead at the end of a transition, angled along its last
// segment. Two strokes rather than a filled triangle: the seam has no
// triangle, and at these sizes the difference is a pixel.
void DrawArrowHead(ID2D1RenderTarget* rt, float x1, float y1, float x2,
                   float y2, float scale, ID2D1Brush* brush) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float ux = dx / len;
    const float uy = dy / len;
    const float size = 7.0f * scale;
    const float spread = 0.5f;
    const float bx = x2 - ux * size;
    const float by = y2 - uy * size;
    rt->DrawLine(D2D1::Point2F(x2, y2),
                 D2D1::Point2F(bx + uy * size * spread, by - ux * size * spread),
                 brush, 1.0f * scale);
    rt->DrawLine(D2D1::Point2F(x2, y2),
                 D2D1::Point2F(bx - uy * size * spread, by + ux * size * spread),
                 brush, 1.0f * scale);
}

}  // namespace

void Renderer::DrawMermaidStateBlock(ID2D1RenderTarget* rt,
                                     IDWriteFactory* dw, const Node& n,
                                     float x, float y, float width) {
    if (!rt || !dw || !n.mermaid_state) return;
    const auto& ls = *n.mermaid_state;
    const Palette& pal = ThemePalette();
    const float pad = mermaid::kMermaidBlockPad;
    const float availW = width > 2.0f * pad ? width - 2.0f * pad : 0.0f;

    // width/height are the drawing's bounding box; the vb fields are
    // the oracle's shim and are not a size to fit.
    const double natural = ls.width;
    float scale = zoom_;
    if (natural > 0 && availW > 0) {
        const float fit = availW / static_cast<float>(natural);
        if (fit < scale) scale = fit;
    }
    const float ox = x;
    const float oy = y + pad;
    auto X = [&](double v) { return ox + static_cast<float>(v * scale); };
    auto Y = [&](double v) { return oy + static_cast<float>(v * scale); };

    ID2D1SolidColorBrush* fill = nullptr;
    ID2D1SolidColorBrush* border = nullptr;
    ID2D1SolidColorBrush* edge = nullptr;
    ID2D1SolidColorBrush* ink = nullptr;
    rt->CreateSolidColorBrush(pal.mermaidNodeFill, &fill);
    rt->CreateSolidColorBrush(pal.mermaidNodeBorder, &border);
    rt->CreateSolidColorBrush(pal.mermaidEdge, &edge);
    rt->CreateSolidColorBrush(pal.mermaidText, &ink);
    if (!fill || !border || !edge || !ink) {
        if (fill) fill->Release();
        if (border) border->Release();
        if (edge) edge->Release();
        if (ink) ink->Release();
        return;
    }

    // Transitions first, so a box sits on top of the line that reaches
    // it, the order mermaid draws in.
    for (const auto& e : ls.edges) {
        for (size_t i = 1; i < e.points.size(); ++i) {
            rt->DrawLine(D2D1::Point2F(X(e.points[i - 1].x), Y(e.points[i - 1].y)),
                         D2D1::Point2F(X(e.points[i].x), Y(e.points[i].y)),
                         edge, 1.5f * scale);
        }
        if (e.points.size() >= 2) {
            const auto& a = e.points[e.points.size() - 2];
            const auto& b = e.points[e.points.size() - 1];
            DrawArrowHead(rt, X(a.x), Y(a.y), X(b.x), Y(b.y), scale, edge);
        }
    }

    for (const auto& node : ls.nodes) {
        if (node.kind == "start" || node.kind == "end") {
            const float r = static_cast<float>(node.r * scale);
            const D2D1_POINT_2F centre = D2D1::Point2F(X(node.cx), Y(node.cy));
            rt->FillEllipse(D2D1::Ellipse(centre, r, r), border);
            if (node.kind == "end") {
                // The end marker carries the ring mermaid gives it.
                const float inner = r * 0.62f;
                rt->FillEllipse(D2D1::Ellipse(centre, inner, inner), fill);
            }
            continue;
        }
        const D2D1_RECT_F rc =
            D2D1::RectF(X(node.cx - node.w * 0.5), Y(node.cy - node.h * 0.5),
                        X(node.cx + node.w * 0.5), Y(node.cy + node.h * 0.5));
        rt->FillRectangle(rc, fill);
        rt->DrawRectangle(rc, border, 1.0f * scale);
        DrawStateText(dw, rt, body_fmt_, node.text, X(node.cx), Y(node.cy),
                      scale, ink);
    }

    // Transition labels sit on the line, at the position the layout
    // gave them.
    for (const auto& label : ls.labels) {
        DrawStateText(dw, rt, body_fmt_, label.text, X(label.x), Y(label.y),
                      scale, ink);
    }

    fill->Release();
    border->Release();
    edge->Release();
    ink->Release();
}
