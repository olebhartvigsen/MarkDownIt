#include "renderer.h"
#include "crash_trace.h"

#include "theme.h"
#include "mermaid/pie_layout.h"
#include "mermaid/layout_cache.h"

#include <cmath>
#include <d2d1.h>
#include <dwrite.h>

// Mermaid default-theme pie palette (pie1..pie12), converted to hex:
// pie1 #ECECFF, pie2 #ffffde, pie3-12 from hsl() strings (#b5ff20...).
// Colors as 0xRRGGBB for D2D1::ColorF.
namespace {

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

constexpr uint32_t kPieTextDark = 0x333333;  // % labels: fill:#333
constexpr float kPieMARGIN = 40.0f;

// UTF-8 → UTF-16 (same helper as the flowchart renderer).
void Utf8To16(const std::string& s, std::u16string& out) {
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n_b = 1;
        if (c < 0x80) { cp = c; n_b = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); n_b = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) |
                 (s[i+2] & 0x3F); n_b = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                 ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); n_b = 4;
        } else { cp = '?'; n_b = 1; }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += n_b;
    }
}

void DrawPieText(IDWriteFactory* dw, ID2D1RenderTarget* rt,
                 IDWriteTextFormat* fmt, const std::string& utf8,
                 float cx, float cy, float scale_x, float scale_y,
                 ID2D1SolidColorBrush* brush, bool center_h, bool center_v,
                 float sizeFactor = 1.0f) {
    if (!fmt) return;
    std::u16string t16;
    Utf8To16(utf8, t16);
    if (t16.empty()) return;
    IDWriteTextFormat* use_fmt = fmt;
    IDWriteTextFormat* scaled = nullptr;
    if (sizeFactor != 1.0f && sizeFactor > 0.01f) {
        // Clone the base format with the scaled font size (resize fits are
        // infrequent; runs per diagram are few dozen).
        WCHAR fam[64] = L"";
        if (SUCCEEDED(fmt->GetFontFamilyName(fam, 64))) {
            DWRITE_FONT_WEIGHT wght = fmt->GetFontWeight();
            DWRITE_FONT_STYLE style = fmt->GetFontStyle();
            DWRITE_FONT_STRETCH stretch = fmt->GetFontStretch();
            float size = fmt->GetFontSize();
            dw->CreateTextFormat(fam, nullptr, wght, style, stretch,
                                 size * sizeFactor, L"", &scaled);
        }
        if (scaled) use_fmt = scaled;
    }
    IDWriteTextLayout* tl = nullptr;
    if (FAILED(dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(t16.data()),
            static_cast<UINT32>(t16.size()), use_fmt,
            1e9f, 1e9f, &tl)) || !tl) return;
    if (scaled) scaled->Release();
    DWRITE_TEXT_METRICS tm{};
    tl->GetMetrics(&tm);
    float w = tm.widthIncludingTrailingWhitespace;
    float h = tm.height;
    float x = cx * scale_x;
    float y = cy * scale_y;
    if (center_h) x -= w * 0.5f;
    if (center_v) y -= h * 0.5f;
    rt->DrawTextLayout(D2D1::Point2F(x, y), tl, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
    tl->Release();
}

}  // namespace

void Renderer::DrawMermaidPieBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                   const Node& n, float x, float y,
                                   float width) {
    if (!rt || !dw) return;
    if (!n.mermaid_pie) return;
    const auto& lp = *n.mermaid_pie;
    const float pad = mermaid::kMermaidBlockPad;
    // Fit the pie into the text column: scale down (never up) when the
    // natural canvas is wider than the available width.
    const float availW =
        width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
    // Lineær zoom: scale = min(zoom, availW/naturalW) — aldrig op-skaler.
    float scale = zoom_;
    if (lp.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(lp.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    float ox = x;
    float oy = y + pad;
    // Text must shrink by the same factor the geometry shrank by.
    const float textScale = scale / (zoom_ > 0.001f ? zoom_ : 1.0f);

    Palette pal = BasePalette();
    ID2D1SolidColorBrush* fillBrush = nullptr;
    ID2D1SolidColorBrush* ringBrush = nullptr;
    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xECECFF), &fillBrush);
    rt->CreateSolidColorBrush(D2D1::ColorF(0x000000), &ringBrush);
    rt->CreateSolidColorBrush(D2D1::ColorF(kPieTextDark), &textBrush);

    diag::Trace("pie: ring");
    // Outer ring (class pieOuterCircle): stroke-only, fill none, 2px.
    if (ringBrush) {
        D2D1_ELLIPSE ring = D2D1::Ellipse(
            D2D1::Point2F(ox + static_cast<float>(lp.cx * scale),
                          oy + static_cast<float>(lp.cy * scale)),
            static_cast<float>(lp.ring_r * scale),
            static_cast<float>(lp.ring_r * scale));
        rt->DrawEllipse(ring, ringBrush, 2.0f * scale);
    }

    diag::Trace("pie: slices");
    // Slices: each arc is a filled path from center, out to arc, back.
    ID2D1Factory* fac = nullptr;
    rt->GetFactory(&fac);
    const double r = lp.radius;
    for (const auto& arc : lp.arcs) {
        if (!fac) break;
        uint32_t hex = kPieColors[arc.color_index %
                                  (sizeof(kPieColors) / sizeof(kPieColors[0]))];
        if (fillBrush)
            fillBrush->SetColor(D2D1::ColorF(hex, 1.0f));
        ID2D1PathGeometry* geom = nullptr;
        if (FAILED(fac->CreatePathGeometry(&geom)) || !geom) continue;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(geom->Open(&sink)) || !sink) { geom->Release(); continue; }

        auto pushPt = [&](double ang, double radius_) {
            double sx = lp.cx + radius_ * std::sin(ang);
            double sy = lp.cy - radius_ * std::cos(ang);
            return D2D1_POINT_2F{
                ox + static_cast<float>(sx * scale),
                oy + static_cast<float>(sy * scale)};
        };

        sink->BeginFigure(
            D2D1::Point2F(ox + static_cast<float>(lp.cx * scale),
                          oy + static_cast<float>(lp.cy * scale)),
            D2D1_FIGURE_BEGIN_FILLED);
        // Line to arc start, arc sweep, line back to center, close.
        D2D1_POINT_2F start = pushPt(arc.start_angle, r);
        sink->AddLine(start);
        if (arc.end_angle - arc.start_angle >= 6.283185307179586) {
            // Full circle: two half arcs (D2D cannot express 2π in one arc).
            D2D1_POINT_2F mid = pushPt(arc.start_angle + 3.141592653589793, r);
            D2D1_POINT_2F back = pushPt(arc.start_angle, r);
            sink->AddArc(D2D1::ArcSegment(
                mid, D2D1::SizeF(static_cast<float>(r * scale),
                                 static_cast<float>(r * scale)),
                0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddArc(D2D1::ArcSegment(
                back, D2D1::SizeF(static_cast<float>(r * scale),
                                 static_cast<float>(r * scale)),
                0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            D2D1_POINT_2F end = pushPt(arc.end_angle, r);
            bool large = (arc.end_angle - arc.start_angle) > 3.141592653589793;
            sink->AddArc(D2D1::ArcSegment(
                end, D2D1::SizeF(static_cast<float>(r * scale),
                                 static_cast<float>(r * scale)),
                0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        if (fillBrush) {
            rt->FillGeometry(geom, fillBrush);
            // Mermaid pie strokes each slice black 2px, opacity 0.7 fill.
            rt->DrawGeometry(geom, ringBrush, 2.0f * scale);
        }
        geom->Release();
        (void)kPieMARGIN;
    }

    // Slice percent labels: 17px trebuchet, fill #333, middle anchor,
    // positioned at 0.75r from center at mid-angle.
    IDWriteTextFormat* slice_fmt = body_fmt_;
    for (const auto& arc : lp.arcs) {
        DrawPieText(dw, rt, slice_fmt, arc.pct,
                    static_cast<float>(arc.label_x),
                    static_cast<float>(arc.label_y),
                    scale, scale, textBrush, true, true, textScale);
    }

    // Title: 25px, black, centered at (0, -200) relative to pie center.
    if (!lp.title.empty()) {
        DrawPieText(dw, rt, body_fmt_, lp.title,
                    static_cast<float>(lp.cx),
                    static_cast<float>(lp.cy - 200.0),
                    scale, scale, textBrush, true, true, textScale);
    }

    // Legend: swatch 18x18 + text at (216, k*22 - offset) with label text
    // at x=22, y=14 (baseline) within the row. With showData, label is
    // already "label [value]" (LayoutPie put that in row.label).
    for (const auto& row : lp.legend) {
        float rx = ox + static_cast<float>(row.x * scale);
        float ry = oy + static_cast<float>(row.y * scale);
        uint32_t hex = kPieColors[row.color_index %
                                  (sizeof(kPieColors) / sizeof(kPieColors[0]))];
        ID2D1SolidColorBrush* swBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(hex, 1.0f), &swBrush);
        if (swBrush) {
            D2D1_RECT_F rc = D2D1::RectF(rx, ry,
                                         rx + 18.0f * scale, ry + 18.0f * scale);
            rt->FillRectangle(rc, swBrush);
            swBrush->Release();
        }
        ID2D1SolidColorBrush* legTxt = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x000000), &legTxt);
        DrawPieText(dw, rt, body_fmt_, row.label,
                    rx + 22.0f * scale, ry + 14.0f * scale,
                    1.0f, 1.0f, legTxt ? legTxt : textBrush, false, true, textScale);
        if (legTxt) legTxt->Release();
    }

    if (fillBrush) fillBrush->Release();
    if (ringBrush) ringBrush->Release();
    if (textBrush) textBrush->Release();
}
