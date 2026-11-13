#include "renderer.h"

#include "theme.h"
#include "mermaid/gantt_layout.h"
#include "mermaid/layout_cache.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>

// Gantt diagram rendering. Geometry comes from LayoutGantt (gantt_layout.cpp,
// shared), this file only paints it with Direct2D, following the same
// pattern renderer_pie.cpp and renderer_sequence.cpp use for their diagrams.
namespace {

constexpr uint32_t kGanttInk = 0x333333;
constexpr uint32_t kGanttGridLine = 0xE0E0E0;
constexpr uint32_t kGanttBandFill = 0xF2F2F2;
constexpr uint32_t kGanttBarDefault = 0x4C9AAE;
constexpr uint32_t kGanttBarCrit = 0xB33A3A;
constexpr uint32_t kGanttBarDone = 0x9AA5AD;
constexpr uint32_t kGanttBarActive = 0x6FB7CC;
constexpr uint32_t kGanttMilestoneFill = 0x333333;

// UTF-8 -> UTF-16 (same helper every mermaid renderer file carries).
void GanttUtf8To16(const std::string& s, std::u16string& out) {
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

// Draws one text run. (x, y) is the left edge when center_h is false, the
// horizontal center otherwise; always vertically centered on y.
void DrawGanttText(IDWriteFactory* dw, ID2D1RenderTarget* rt,
                   IDWriteTextFormat* fmt, const std::string& utf8,
                   float x, float y, ID2D1SolidColorBrush* brush,
                   bool center_h, float sizeFactor = 1.0f) {
    if (!fmt || !brush || utf8.empty()) return;
    std::u16string t16;
    GanttUtf8To16(utf8, t16);
    if (t16.empty()) return;
    IDWriteTextFormat* use_fmt = fmt;
    IDWriteTextFormat* scaled = nullptr;
    if (sizeFactor != 1.0f && sizeFactor > 0.01f) {
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
            1e9f, 1e9f, &tl)) || !tl) {
        if (scaled) scaled->Release();
        return;
    }
    if (scaled) scaled->Release();
    DWRITE_TEXT_METRICS tm{};
    tl->GetMetrics(&tm);
    float w = tm.widthIncludingTrailingWhitespace;
    float h = tm.height;
    float px = x;
    float py = y - h * 0.5f;
    if (center_h) px -= w * 0.5f;
    rt->DrawTextLayout(D2D1::Point2F(px, py), tl, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
    tl->Release();
}

}  // namespace

void Renderer::DrawMermaidGanttBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                     const Node& n, float x, float y,
                                     float width) {
    if (!rt || !dw) return;
    if (!n.mermaid_gantt) return;
    const auto& lg = *n.mermaid_gantt;
    const float pad = mermaid::kMermaidBlockPad;
    const float availW = width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
    float scale = zoom_;
    if (lg.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(lg.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    const float ox = x;
    const float oy = y + pad;
    auto X = [&](double v) { return ox + static_cast<float>(v * scale); };
    auto Y = [&](double v) { return oy + static_cast<float>(v * scale); };
    const float textScale = scale / (zoom_ > 0.001f ? zoom_ : 1.0f);

    ID2D1SolidColorBrush* ink = nullptr;
    ID2D1SolidColorBrush* gridLine = nullptr;
    ID2D1SolidColorBrush* bandFill = nullptr;
    ID2D1SolidColorBrush* barBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(kGanttInk), &ink);
    rt->CreateSolidColorBrush(D2D1::ColorF(kGanttGridLine), &gridLine);
    rt->CreateSolidColorBrush(D2D1::ColorF(kGanttBandFill), &bandFill);
    rt->CreateSolidColorBrush(D2D1::ColorF(kGanttBarDefault), &barBrush);
    if (!ink || !gridLine || !bandFill || !barBrush) {
        if (ink) ink->Release();
        if (gridLine) gridLine->Release();
        if (bandFill) bandFill->Release();
        if (barBrush) barBrush->Release();
        return;
    }

    if (!lg.title.empty()) {
        DrawGanttText(dw, rt, body_fmt_, lg.title, X(lg.title_x),
                      Y(lg.title_y), ink, false, textScale * 1.1f);
    }

    for (const auto& tick : lg.ticks) {
        rt->DrawLine(D2D1::Point2F(X(tick.x), Y(lg.grid_top)),
                     D2D1::Point2F(X(tick.x), Y(lg.grid_bottom)), gridLine,
                     1.0f * scale);
        DrawGanttText(dw, rt, body_fmt_, tick.label, X(tick.x),
                      Y(lg.grid_bottom) + 10.0f * scale, ink, true, textScale);
    }

    for (const auto& band : lg.sections) {
        if (band.color_index % 2 == 1) {
            D2D1_RECT_F rc = D2D1::RectF(X(0.0), Y(band.y),
                                         X(lg.width), Y(band.y + band.height));
            rt->FillRectangle(rc, bandFill);
        }
        if (!band.title.empty()) {
            DrawGanttText(dw, rt, body_fmt_, band.title, X(band.title_x),
                          Y(band.title_y), ink, false, textScale);
        }
    }

    for (const auto& bar : lg.bars) {
        uint32_t fillHex = kGanttBarDefault;
        if (bar.crit) fillHex = kGanttBarCrit;
        else if (bar.done) fillHex = kGanttBarDone;
        else if (bar.active) fillHex = kGanttBarActive;
        barBrush->SetColor(D2D1::ColorF(bar.milestone ? kGanttMilestoneFill
                                                       : fillHex));
        if (bar.milestone) {
            const float side = static_cast<float>(bar.height * scale);
            const float cx = X(bar.x) + side * 0.5f;
            const float cy = Y(bar.y) + side * 0.5f;
            const float half = side * 0.5f;
            ID2D1Factory* fac = nullptr;
            rt->GetFactory(&fac);
            if (fac) {
                ID2D1PathGeometry* geom = nullptr;
                if (SUCCEEDED(fac->CreatePathGeometry(&geom)) && geom) {
                    ID2D1GeometrySink* sink = nullptr;
                    if (SUCCEEDED(geom->Open(&sink)) && sink) {
                        sink->BeginFigure(D2D1::Point2F(cx, cy - half),
                                          D2D1_FIGURE_BEGIN_FILLED);
                        sink->AddLine(D2D1::Point2F(cx + half, cy));
                        sink->AddLine(D2D1::Point2F(cx, cy + half));
                        sink->AddLine(D2D1::Point2F(cx - half, cy));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close();
                        sink->Release();
                        rt->FillGeometry(geom, barBrush);
                    }
                    geom->Release();
                }
                fac->Release();
            }
        } else {
            D2D1_RECT_F rc = D2D1::RectF(X(bar.x), Y(bar.y),
                                         X(bar.x) + static_cast<float>(bar.width * scale),
                                         Y(bar.y) + static_cast<float>(bar.height * scale));
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, 3.0f * scale, 3.0f * scale);
            rt->FillRoundedRectangle(rr, barBrush);
        }
        if (!bar.label.empty()) {
            DrawGanttText(dw, rt, body_fmt_, bar.label, X(bar.label_x),
                          Y(bar.label_y), ink, false, textScale);
        }
    }

    ink->Release();
    gridLine->Release();
    bandFill->Release();
    barBrush->Release();
}
