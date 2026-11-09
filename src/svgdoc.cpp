#include "svgdoc.h"
#include "crash_trace.h"

#include <windows.h>
#include <d2d1_1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Win32 maps DrawText to DrawTextW/A; the portable seam does not.
#undef DrawText

namespace svg {
namespace {

using Microsoft::WRL::ComPtr;

// Invalid UTF-8 is replaced by U+FFFD by Windows 10/11 with flags == 0.
// Explicit lengths preserve embedded NULs and supplementary characters.
std::wstring Utf16(const std::string& utf8) {
    if (utf8.empty()) return {};
    if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::length_error("SVG text exceeds the UTF-8 conversion limit");
    }
    const int size = static_cast<int>(utf8.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    if (!count) throw std::runtime_error("SVG UTF-8 conversion failed");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, result.data(), count) != count) {
        throw std::runtime_error("SVG UTF-8 conversion failed");
    }
    return result;
}

bool SameFont(const platform::FontSpec& a, const platform::FontSpec& b) {
    return a.family == b.family && a.size == b.size && a.bold == b.bold &&
           a.italic == b.italic && a.monospace == b.monospace;
}

// CoreGraphicsTarget draws one CTLine, even when a text node contains a
// character-reference newline. Keep hard breaks from creating extra native
// DirectWrite baselines. The shared scene measures only its first text line.
std::string SingleLine(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) if (c != '\r' && c != '\n') result.push_back(c);
    return result;
}

struct DrawStatus {
    std::size_t failures = 0;
    HRESULT firstFailure = S_OK;

    bool Check(HRESULT hr) {
        if (SUCCEEDED(hr)) return true;
        if (!failures) firstFailure = hr;
        ++failures;
        return false;
    }
};

// Private to the SVG adapter, not a general markdown/editor TextEngine.
// SvgDoc only measures a single unwrapped run; unsupported editor seams fail
// explicitly rather than returning invented hit-test positions.
class SvgTextEngine final : public platform::TextEngine {
public:
    struct Layout {
        ComPtr<IDWriteTextLayout> native;
        float width = 0.0f;
        float height = 0.0f;
        float baseline = 0.0f;
    };

    SvgTextEngine(IDWriteFactory* factory, DrawStatus& status)
        : factory_(factory), status_(status) {
        if (factory_) status_.Check(factory_->GetSystemFontCollection(&fonts_));
    }

    const Layout* GetLayout(const std::string& text, const platform::FontSpec& font) const {
        for (const auto& entry : layouts_) {
            if (entry.text == text && SameFont(entry.font, font)) return &entry.layout;
        }
        if (!factory_) {
            status_.Check(E_POINTER);
            return nullptr;
        }
        if (!std::isfinite(font.size) || font.size <= 0.0f) {
            status_.Check(E_INVALIDARG);
            return nullptr;
        }

        IDWriteTextFormat* format = nullptr;
        for (const auto& entry : formats_) {
            if (SameFont(entry.font, font)) {
                format = entry.native.Get();
                break;
            }
        }
        if (!format) {
            FormatEntry entry;
            entry.font = font;
            const std::wstring family = ResolveFamily(font);
            if (!status_.Check(factory_->CreateTextFormat(
                    family.c_str(), fonts_.Get(),
                    font.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
                    font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, font.size, L"", &entry.native))) {
                return nullptr;
            }
            if (!status_.Check(entry.native->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))) {
                return nullptr;
            }
            format = entry.native.Get();
            formats_.push_back(std::move(entry));
        }

        const std::wstring wide = Utf16(SingleLine(text));
        LayoutEntry entry;
        entry.text = text;
        entry.font = font;
        // Match the exact unbounded request made by the shared scene. The
        // same cached layout supplies both anchor width and painted glyphs.
        if (!status_.Check(factory_->CreateTextLayout(
                wide.data(), static_cast<UINT32>(wide.size()), format,
                1.0e9f, 1.0e9f, &entry.layout.native))) return nullptr;
        DWRITE_TEXT_METRICS metrics = {};
        if (!status_.Check(entry.layout.native->GetMetrics(&metrics))) return nullptr;
        UINT32 count = 0;
        // The size query normally returns E_NOT_SUFFICIENT_BUFFER.
        const HRESULT query = entry.layout.native->GetLineMetrics(nullptr, 0, &count);
        if (!count) {
            status_.Check(FAILED(query) ? query : E_FAIL);
            return nullptr;
        }
        std::vector<DWRITE_LINE_METRICS> lines(count);
        if (!status_.Check(entry.layout.native->GetLineMetrics(lines.data(), count, &count))) {
            return nullptr;
        }
        entry.layout.width = metrics.widthIncludingTrailingWhitespace;
        entry.layout.height = metrics.height;
        entry.layout.baseline = lines.front().baseline;
        layouts_.push_back(std::move(entry));
        return &layouts_.back().layout;
    }

    std::vector<platform::TextLine> MeasureWrapped(const std::vector<platform::GlyphRun>& runs,
                                             float maxWidth, float lineSpacing) override {
        if (runs.size() != 1 || maxWidth != 1.0e9f || lineSpacing != 0.0f) {
            throw std::logic_error("SVG adapter only measures the shared scene's unwrapped run");
        }
        const auto& text = runs.front().text;
        const std::string first = text.substr(0, text.find_first_of("\r\n"));
        const Layout* layout = GetLayout(first.empty() ? " " : first, runs.front().font);
        if (!layout) return {};
        platform::TextLine line;
        line.width = first.empty() ? 0.0f : layout->width;
        line.height = layout->height;
        line.ascent = layout->baseline;
        line.runs = runs;
        return {std::move(line)};
    }

    uint32_t HitTestOffset(const platform::TextLine&, float, bool*) const override {
        throw std::logic_error("SVG adapter does not provide editor hit testing");
    }
    platform::Rect OffsetToRect(const platform::TextLine&, uint32_t) const override {
        throw std::logic_error("SVG adapter does not provide editor offset rectangles");
    }
    float LineHeight(const platform::FontSpec& font) const override {
        const Layout* layout = GetLayout(" ", font);
        return layout ? layout->height : 0.0f;
    }

private:
    bool Installed(const std::wstring& family) const {
        if (!fonts_ || family.empty()) return false;
        UINT32 index = 0;
        BOOL exists = FALSE;
        return status_.Check(fonts_->FindFamilyName(family.c_str(), &index, &exists)) && exists;
    }

    std::wstring ResolveFamily(const platform::FontSpec& font) const {
        std::wstring family = Utf16(font.family);
        // Prefer the same installed named face, including Helvetica/Menlo.
        // Only substitute absent Mac families. Any other missing family uses
        // the native UI fallback. Native metrics/rasterization are not pixel
        // identical to Core Text, even with an identically named font.
        if (Installed(family)) return family;
        auto equal = [&](const wchar_t* name) {
            return CompareStringOrdinal(family.c_str(), -1, name, -1, TRUE) == CSTR_EQUAL;
        };
        if (equal(L"Helvetica") || equal(L"Helvetica Neue")) family = L"Arial";
        else if (equal(L"Menlo")) family = L"Consolas";
        else family = L"Segoe UI";
        return Installed(family) ? family : std::wstring(L"Segoe UI");
    }

    struct FormatEntry {
        platform::FontSpec font;
        ComPtr<IDWriteTextFormat> native;
    };
    struct LayoutEntry {
        std::string text;
        platform::FontSpec font;
        Layout layout;
    };
    IDWriteFactory* factory_;
    DrawStatus& status_;
    ComPtr<IDWriteFontCollection> fonts_;
    mutable std::vector<FormatEntry> formats_;
    mutable std::vector<LayoutEntry> layouts_;
};

D2D1_RECT_F Bounds(const platform::Rect& rect) {
    return D2D1::RectF((std::min)(rect.x, rect.x + rect.width),
                       (std::min)(rect.y, rect.y + rect.height),
                       (std::max)(rect.x, rect.x + rect.width),
                       (std::max)(rect.y, rect.y + rect.height));
}

D2D1_ROUNDED_RECT Rounded(const platform::Rect& rect, float radius) {
    const auto bounds = Bounds(rect);
    const float r = (std::max)(0.0f, (std::min)(radius,
        (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top) * 0.5f));
    return D2D1::RoundedRect(bounds, r, r);
}

D2D1_ELLIPSE Ellipse(const platform::Rect& rect) {
    return D2D1::Ellipse(D2D1::Point2F(rect.x + rect.width * 0.5f,
                                    rect.y + rect.height * 0.5f),
                         std::abs(rect.width) * 0.5f, std::abs(rect.height) * 0.5f);
}

class SvgRenderTarget final : public platform::RenderTarget {
public:
    SvgRenderTarget(ID2D1DeviceContext5* context, SvgTextEngine& text, DrawStatus& status,
                    platform::RenderTarget* trace)
        : context_(context), text_(text), status_(status), trace_(trace) {
        // The scene has already fitted every coordinate. Never install a
        // wrapper document transform: all primitives inherit the caller's.
        context_->GetFactory(&factory_);
        status_.Check(context_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush_));
    }

    void FillRect(const platform::Rect& rect, const platform::Color& color) override {
        if (trace_) trace_->FillRect(rect, color);
        if (Brush(color)) context_->FillRectangle(Bounds(rect), brush_.Get());
    }
    void FillRoundedRect(const platform::Rect& rect, float radius,
                         const platform::Color& color) override {
        if (trace_) trace_->FillRoundedRect(rect, radius, color);
        if (Brush(color)) context_->FillRoundedRectangle(Rounded(rect, radius), brush_.Get());
    }
    void StrokeRect(const platform::Rect& rect, const platform::Color& color, float width) override {
        if (trace_) trace_->StrokeRect(rect, color, width);
        if (Brush(color)) context_->DrawRectangle(Bounds(rect), brush_.Get(), width);
    }
    void StrokeRoundedRect(const platform::Rect& rect, float radius,
                           const platform::Color& color, float width) override {
        if (trace_) trace_->StrokeRoundedRect(rect, radius, color, width);
        if (Brush(color)) context_->DrawRoundedRectangle(
            Rounded(rect, radius), brush_.Get(), width > 0.0f ? width : 1.0f);
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const platform::Color& color, float width) override {
        if (trace_) trace_->StrokeLine(x0, y0, x1, y1, color, width);
        if (Brush(color)) context_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1),
                                             brush_.Get(), width);
    }
    void DrawText(const std::string& utf8, const platform::FontSpec& font,
                  float x, float baselineY, const platform::Color& color) override {
        if (trace_) trace_->DrawText(utf8, font, x, baselineY, color);
        if (SingleLine(utf8).empty() || !Brush(color)) return;
        const auto* layout = text_.GetLayout(utf8, font);
        if (!layout) return;
        context_->DrawTextLayout(D2D1::Point2F(x, baselineY - layout->baseline),
                                layout->native.Get(), brush_.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    void DrawBitmap(platform::BitmapHandle, const platform::Rect&) override {
        // The reference SVG scene does not emit bitmap drawing operations.
        throw std::logic_error("SVG adapter does not support bitmap drawing");
    }
    void FillEllipse(const platform::Rect& rect, const platform::Color& color) override {
        if (trace_) trace_->FillEllipse(rect, color);
        if (Brush(color)) context_->FillEllipse(Ellipse(rect), brush_.Get());
    }
    void StrokeEllipse(const platform::Rect& rect, const platform::Color& color, float width) override {
        if (trace_) trace_->StrokeEllipse(rect, color, width);
        if (Brush(color)) context_->DrawEllipse(Ellipse(rect), brush_.Get(), width);
    }
    void FillPolygon(const std::vector<platform::Point>& points, const platform::Color& color) override {
        if (trace_) trace_->FillPolygon(points, color);
        if (points.size() < 3 || !Brush(color)) return;
        const auto path = Polygon(points, true, true);
        if (path) context_->FillGeometry(path.Get(), brush_.Get());
    }
    void StrokePolygon(const std::vector<platform::Point>& points, const platform::Color& color,
                       float width, bool closed) override {
        if (trace_) trace_->StrokePolygon(points, color, width, closed);
        if (points.size() < 2 || !Brush(color)) return;
        const auto path = Polygon(points, closed, false);
        if (path) context_->DrawGeometry(path.Get(), brush_.Get(), width > 0.0f ? width : 1.0f);
    }
    void FillPieSlice(const platform::Rect&, float, float, const platform::Color&) override {
        // No pie-slice operation is emitted by platform::SvgDoc, either.
        throw std::logic_error("SVG adapter does not support pie-slice operations");
    }
    void PushClip(const platform::Rect& rect) override {
        if (trace_) trace_->PushClip(rect);
        context_->PushAxisAlignedClip(Bounds(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    void PopClip() override {
        if (trace_) trace_->PopClip();
        context_->PopAxisAlignedClip();
    }

private:
    bool Brush(const platform::Color& color) {
        if (!brush_) return false;
        brush_->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
        return true;
    }

    ComPtr<ID2D1PathGeometry> Polygon(const std::vector<platform::Point>& points,
                                     bool closed, bool filled) {
        ComPtr<ID2D1PathGeometry> path;
        if (!factory_) {
            status_.Check(E_POINTER);
            return path;
        }
        if (!status_.Check(factory_->CreatePathGeometry(&path))) return {};
        ComPtr<ID2D1GeometrySink> sink;
        if (!status_.Check(path->Open(&sink))) return {};
        // Core Graphics fills with the nonzero winding rule, not even-odd.
        // Dashed paths have already been split into segments by the scene.
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(D2D1::Point2F(points.front().x, points.front().y),
                          filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        for (std::size_t i = 1; i < points.size(); ++i) {
            sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
        }
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        if (!status_.Check(sink->Close())) return {};
        return path;
    }

    ID2D1DeviceContext5* context_;
    SvgTextEngine& text_;
    DrawStatus& status_;
    platform::RenderTarget* trace_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1SolidColorBrush> brush_;
};

}  // namespace

SvgDoc::~SvgDoc() = default;

void SvgDoc::Release() {
    scene_.reset();
}

bool SvgDoc::Load(ID2D1DeviceContext5* ctx, const std::string& xml) {
    Release();
    if (!ctx || xml.empty()) return false;
    auto scene = std::make_unique<platform::SvgDoc>();
    const bool loaded = scene->Load(xml);
    diag::TraceFmt("SVGDOC mode=shared-scene loaded=%d shapes=%zu w=%g h=%g",
                   loaded ? 1 : 0, scene->shapes().size(), scene->Width(), scene->Height());
    if (!loaded) return false;
    scene_ = std::move(scene);
    return true;
}

void SvgDoc::Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                  float x, float y, float w, float h,
                  platform::RenderTarget* trace, platform::TextEngine* metrics) {
    if (!ctx || !scene_) return;
    DrawStatus status;
    SvgTextEngine text(dw, status);
    SvgRenderTarget target(ctx, text, status, trace);
    // The adapter never changes the context transform, including on failure.
    // Scene limitations (including transformed text) are intentionally those
    // of the unmodified Mac reference, not a second Windows interpretation.
    scene_->Draw(&target, metrics ? metrics : &text, x, y, w, h);
    diag::TraceFmt("SVGDOC draw mode=shared-scene x=%g y=%g w=%g h=%g shapes=%zu failures=%zu hr=0x%08lx",
                   x, y, w, h, scene_->shapes().size(), status.failures,
                   static_cast<unsigned long>(status.firstFailure));
    if (trace && status.failures) {
        throw std::runtime_error("SVG native adapter failed during parity recording");
    }
}

}  // namespace svg
