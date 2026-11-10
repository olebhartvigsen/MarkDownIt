// The Direct2D half of the RenderTarget seam, the macOS twin being CoreGraphicsTarget
// in src/macos/cg_target_mac.h. Coordinates are DIPs with y down; BeginDraw/EndDraw and
// transforms stay with the caller, as renderer.cpp already assumes.

#include "windows/render_target_win.h"

#ifdef MARKDOWNIT_LOCAL_SYNTAX
// Throwaway d2d/dwrite shapes for the g++ syntax check only, never real.
#include "/tmp/d2d_stub.h"
#else
#include <d2d1.h>
#include <dwrite.h>
#endif

#include <algorithm>

// The platform headers above re-define the DrawText macro after the
// header removed it, and the method definitions here must keep the
// seam's spelling.
#ifdef DrawText
#undef DrawText
#endif
#include <cctype>
#include <cmath>

namespace platform {
namespace {

constexpr double kPi = 3.14159265358979323846;

D2D1_RECT_F Box(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.width, r.y + r.height); }

D2D1_ELLIPSE EllipseOf(const Rect& r) {  // the ellipse that fits, the seam's rule
    return D2D1::Ellipse(D2D1::Point2F(r.x + r.width * 0.5f, r.y + r.height * 0.5f),
                         r.width * 0.5f, r.height * 0.5f);
}

// The DWriteEngine's UTF-8 to UTF-16 converter, verbatim: what draws
// is what the engine measured.
std::wstring ToUtf16(const std::string& text) {
    std::wstring out; out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        unsigned int cp = 0; size_t extra = 0;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
        else { cp = 0xFFFDu; extra = 0; }  // a stray byte is one replacement char
        for (size_t k = 0; k < extra && i + 1 + k < text.size(); ++k) {
            unsigned char cc = (unsigned char)text[i + 1 + k];
            if ((cc & 0xC0) != 0x80) { extra = k; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += 1 + extra;
        if (cp > 0x10FFFFu) cp = 0xFFFDu;
        if (cp < 0x10000u) out.push_back((wchar_t)cp);
        else {
            cp -= 0x10000u;
            out.push_back((wchar_t)(0xD800u + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

// The DWriteEngine's family mapping, verbatim: the font that draws is
// what it measured; monospace via the "code" family name.
std::wstring FamilyFor(const std::string& family) {
    std::string f = family;
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (f == "code" || f == "mono" || f == "consolas") return L"Consolas";
    if (f == "serif" || f == "georgia") return L"Georgia";
    return L"Segoe UI";  // body, headings, ui and anything unknown
}

// Builds one path on the target's factory, draws it once (filled or
// stroked); failures draw nothing, releasing only what they took.
template <typename Build>
void DrawPath(ID2D1RenderTarget* target, ID2D1SolidColorBrush* fill,
              ID2D1SolidColorBrush* stroke, float width, Build build) {
    if (!target) return;
    ID2D1Factory* factory = nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    target->GetFactory(&factory);
    if (!factory || FAILED(factory->CreatePathGeometry(&geometry)) || !geometry) return;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) { geometry->Release(); factory->Release(); return; }
    build(sink);
    sink->Close();
    sink->Release();
    if (fill) target->FillGeometry(geometry, fill);
    if (stroke) target->DrawGeometry(geometry, stroke, width > 0.0f ? width : 1.0f);
    geometry->Release();
    factory->Release();
}

}  // namespace

D2DTarget::D2DTarget(ID2D1RenderTarget* target) : target_(target) {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&write_factory_)))) {
        write_factory_ = nullptr;  // every later call degrades to empty
    }
}

D2DTarget::D2DTarget(ID2D1RenderTarget* target, IDWriteFactory* factory)
    : target_(target) {
    if (factory) {
        write_factory_ = factory;
        write_factory_->AddRef();
    } else if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(&write_factory_)))) {
        write_factory_ = nullptr;  // every later call degrades to empty
    }
}

D2DTarget::~D2DTarget() {
    for (auto& entry : formats_) if (entry.second) entry.second->Release();
    if (write_factory_) write_factory_->Release();
}

ID2D1SolidColorBrush* D2DTarget::BrushFor(const Color& color) const {
    ID2D1SolidColorBrush* brush = nullptr;
    if (!target_) return nullptr;
    target_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    return brush;
}

IDWriteTextFormat* D2DTarget::FormatFor(const FontSpec& font) {
    if (!write_factory_) return nullptr;
    const std::wstring family = FamilyFor(font.family);
    const float size = font.size > 0.0f ? font.size : 14.0f;
    const int weight = font.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    const int style = font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    const std::tuple<std::wstring, float, int, int> key(family, size, weight, style);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* format = nullptr;
    if (FAILED(write_factory_->CreateTextFormat(
            family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight),
            static_cast<DWRITE_FONT_STYLE>(style), DWRITE_FONT_STRETCH_NORMAL, size, L"", &format)))
        format = nullptr;  // remembered, like the engine's null formats
    if (format) {  // one unbreakable left aligned line, the engine's wrap
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    formats_[key] = format;
    return format;
}

void D2DTarget::FillRect(const Rect& rect, const Color& color) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    target_->FillRectangle(Box(rect), brush);
    brush->Release();
}

void D2DTarget::FillRoundedRect(const Rect& rect, float radius, const Color& color) {
    if (radius <= 0.0f) { FillRect(rect, color); return; }
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    const float r = std::min(radius, std::min(rect.width, rect.height) * 0.5f);  // never overrun, as on mac
    target_->FillRoundedRectangle(D2D1::RoundedRect(Box(rect), r, r), brush);
    brush->Release();
}

void D2DTarget::FillRoundedRectSides(const Rect& rect, float radius, const Color& color,
                                     bool roundLeft, bool roundRight) {
    if (radius <= 0.0f || (!roundLeft && !roundRight)) { FillRect(rect, color); return; }
    if (roundLeft && roundRight) { FillRoundedRect(rect, radius, color); return; }
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    const float r = std::min(radius, std::min(rect.width, rect.height) * 0.5f);
    // A rounded side gets a rounded-rect cap, width 2r; its outer corners
    // round like the pill's and the inner ones get overfilled by the full
    // height middle span. Fills overlap by r inside the shape: safe for
    // today's opaque seam backgrounds; the mac twin's one path is the fix.
    const Rect leftCap{rect.x, rect.y, 2.0f * r, rect.height};
    const Rect rightCap{rect.x + rect.width - 2.0f * r, rect.y, 2.0f * r, rect.height};
    if (roundLeft) target_->FillRoundedRectangle(D2D1::RoundedRect(Box(leftCap), r, r), brush);
    if (roundRight) target_->FillRoundedRectangle(D2D1::RoundedRect(Box(rightCap), r, r), brush);
    const float left = roundLeft ? rect.x + r : rect.x;
    const float right = roundRight ? rect.x + rect.width - r : rect.x + rect.width;
    target_->FillRectangle(D2D1::RectF(left, rect.y, right, rect.y + rect.height), brush);
    brush->Release();
}

void D2DTarget::StrokeRect(const Rect& rect, const Color& color, float lineWidth) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    target_->DrawRectangle(Box(rect), brush, lineWidth > 0.0f ? lineWidth : 1.0f);
    brush->Release();
}

void D2DTarget::StrokeRoundedRect(const Rect& rect, float radius, const Color& color, float lineWidth) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    const float r = radius > 0.0f ? radius : 0.0f;  // zero is square, as on mac
    target_->DrawRoundedRectangle(D2D1::RoundedRect(Box(rect), r, r), brush, lineWidth > 0.0f ? lineWidth : 1.0f);
    brush->Release();
}

void D2DTarget::StrokeLine(float x0, float y0, float x1, float y1, const Color& color, float lineWidth) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    target_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush, lineWidth > 0.0f ? lineWidth : 1.0f);
    brush->Release();
}

void D2DTarget::DrawText(const std::string& utf8, const FontSpec& font, float x, float baselineY,
                         const Color& color) {
    if (utf8.empty() || !target_ || !write_factory_) return;
    IDWriteTextFormat* format = FormatFor(font);
    if (!format) return;
    const std::wstring text = ToUtf16(utf8);
    if (text.empty()) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(write_factory_->CreateTextLayout(text.c_str(), (UINT32)text.size(), format,
                                                100000.0f, 1000.0f, &layout)) || !layout)
        return;
    // The seam places the BASELINE at baselineY while DrawTextLayout takes the top of
    // the line's box: draw at y minus the first line's baseline metric. The no wrap
    // format makes line one the run; the font size is the fallback ascent.
    DWRITE_LINE_METRICS metrics = {}; UINT32 lineCount = 0;
    float ascent = font.size > 0.0f ? font.size : 14.0f;
    if (SUCCEEDED(layout->GetLineMetrics(&metrics, 1, &lineCount)) && lineCount > 0) ascent = metrics.baseline;
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (brush) {
        target_->DrawTextLayout(D2D1::Point2F(x, baselineY - ascent), layout, brush);
        brush->Release();
    }
    layout->Release();
}

void D2DTarget::DrawBitmap(BitmapHandle image, const Rect& rect) {
    if (!target_ || !image) return;
    // The Windows image caches own decoded raster images as ID2D1Bitmap,
    // so a handle here is one of those. ImageKind::Vector is unsupported
    // on this twin yet: nothing is drawn for it.
    ID2D1Bitmap* bitmap = static_cast<ID2D1Bitmap*>(image);
    const D2D1_SIZE_F size = bitmap->GetSize();
    if (!(size.width > 0.0f) || !(size.height > 0.0f) || !(rect.width > 0.0f) || !(rect.height > 0.0f)) return;
    // Keep the aspect and centre in the rectangle, the mac twin's rule.
    const float scale = std::min(rect.width / size.width, rect.height / size.height);
    const float w = size.width * scale, h = size.height * scale;
    const D2D1_RECT_F dest = D2D1::RectF(rect.x + (rect.width - w) * 0.5f, rect.y + (rect.height - h) * 0.5f,
                                         rect.x + (rect.width + w) * 0.5f, rect.y + (rect.height + h) * 0.5f);
    target_->DrawBitmap(bitmap, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        D2D1::RectF(0.0f, 0.0f, size.width, size.height));
}

void D2DTarget::FillEllipse(const Rect& rect, const Color& color) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    target_->FillEllipse(EllipseOf(rect), brush);
    brush->Release();
}

void D2DTarget::StrokeEllipse(const Rect& rect, const Color& color, float lineWidth) {
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    target_->DrawEllipse(EllipseOf(rect), brush, lineWidth > 0.0f ? lineWidth : 1.0f);
    brush->Release();
}

void D2DTarget::FillPolygon(const std::vector<Point>& points, const Color& color) {
    if (points.size() < 3) return;
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    DrawPath(target_, brush, nullptr, 0.0f, [&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(D2D1::Point2F(points.front().x, points.front().y), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < points.size(); ++i)
            sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);  // joins last back to first
    });
    brush->Release();
}

void D2DTarget::StrokePolygon(const std::vector<Point>& points, const Color& color, float lineWidth, bool closed) {
    if (points.size() < 2) return;
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    DrawPath(target_, nullptr, brush, lineWidth, [&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(D2D1::Point2F(points.front().x, points.front().y), D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i < points.size(); ++i)
            sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    });
    brush->Release();
}

void D2DTarget::FillPieSlice(const Rect& rect, float startAngle, float endAngle, const Color& color) {
    if (!target_ || !(rect.width > 0.0f) || !(rect.height > 0.0f)) return;
    ID2D1SolidColorBrush* brush = BrushFor(color);
    if (!brush) return;
    const float radius = std::min(rect.width, rect.height) * 0.5f;
    const float cx = rect.x + rect.width * 0.5f, cy = rect.y + rect.height * 0.5f;
    // The seam counts degrees clockwise from three o'clock, which in this
    // y-down space is the plain angle, the math the pie block runs on its
    // canvas; the mac twin negates its angles because Core Graphics counts
    // the other way. Wedge: centre, arc start, around, close.
    const auto pointAt = [&](float deg) {
        const double t = deg * kPi / 180.0;
        return D2D1::Point2F(cx + (float)(radius * std::cos(t)), cy + (float)(radius * std::sin(t)));
    };
    DrawPath(target_, brush, nullptr, 0.0f, [&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(pointAt(startAngle));
        const float sweep = endAngle - startAngle;
        if (sweep >= 360.0f) {  // a whole circle: two half arcs, as the pie block splits
            sink->AddArc(D2D1::ArcSegment(pointAt(startAngle + 180.0f), D2D1::SizeF(radius, radius),
                                          0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddArc(D2D1::ArcSegment(pointAt(startAngle), D2D1::SizeF(radius, radius), 0.0f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            sink->AddArc(D2D1::ArcSegment(pointAt(endAngle), D2D1::SizeF(radius, radius), 0.0f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE, sweep > 180.0f
                                          ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    brush->Release();
}

void D2DTarget::PushClip(const Rect& rect) {
    if (!target_) return;
    target_->PushAxisAlignedClip(Box(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void D2DTarget::PopClip() {
    if (!target_) return;
    target_->PopAxisAlignedClip();
}

}  // namespace platform
