// Records the macOS reference renderer's drawing commands, without a display.
// Fixed text metrics isolate scene parity from platform font rasterization.
#include "macos/svg_mac.h"
#include "svg_native_fixture.h"

#ifdef _WIN32
#include "svgdoc.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#undef DrawText
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string Json(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

class FixedMetrics final : public platform::TextEngine {
public:
    std::vector<platform::TextLine> MeasureWrapped(
        const std::vector<platform::GlyphRun>& runs, float, float) override {
        platform::TextLine line;
        line.runs = runs;
        for (const auto& run : runs) {
            std::size_t count = 0;
            for (unsigned char c : run.text) if ((c & 0xc0) != 0x80) ++count;
            line.width += static_cast<float>(count) * run.font.size * 0.5f;
            line.height = std::max(line.height, run.font.size);
            line.ascent = std::max(line.ascent, run.font.size * 0.8f);
        }
        return {line};
    }
    uint32_t HitTestOffset(const platform::TextLine&, float, bool*) const override {
        throw std::logic_error("SVG must not request hit testing");
    }
    platform::Rect OffsetToRect(const platform::TextLine&, uint32_t) const override {
        throw std::logic_error("SVG must not request caret placement");
    }
    float LineHeight(const platform::FontSpec& font) const override { return font.size; }
};

class Commands final : public platform::RenderTarget {
public:
    std::vector<std::string> ops;
    // Record red shafts and green markers apart from black box strokes.
    std::vector<platform::Point> relationPoints;
    std::vector<platform::Point> markerPoints;

    void CheckArrowContacts(float zoom, int translated) const {
        const float dx = translated ? 67.0f : 0.0f;
        const float dy = translated ? 49.0f : 0.0f;
        for (const auto& arrow : svg_native_fixture::kArrows) {
            for (const bool markerOnly : {false, true}) {
                float left = 1.0e9f, top = 1.0e9f;
                float right = -1.0e9f, bottom = -1.0e9f;
                std::size_t count = 0;
                const auto& points = markerOnly ? markerPoints : relationPoints;
                for (const auto& point : points) {
                    if (point.x < dx + arrow.left * zoom ||
                        point.x >= dx + arrow.right * zoom ||
                        point.y < dy + arrow.top * zoom ||
                        point.y >= dy + arrow.bottom * zoom) continue;
                    left = std::min(left, point.x);
                    top = std::min(top, point.y);
                    right = std::max(right, point.x);
                    bottom = std::max(bottom, point.y);
                    ++count;
                }
                const float expectedLeft = markerOnly ? arrow.expectedMarkerLeft : arrow.expectedLeft;
                const float expectedTop = markerOnly ? arrow.expectedMarkerTop : arrow.expectedTop;
                const float expectedRight = markerOnly ? arrow.expectedMarkerRight : arrow.expectedRight;
                const float expectedBottom = markerOnly ? arrow.expectedMarkerBottom : arrow.expectedBottom;
                if (count < (markerOnly ? 3u : 6u) ||
                    std::abs(left - (dx + expectedLeft * zoom)) > 0.001f ||
                    std::abs(top - (dy + expectedTop * zoom)) > 0.001f ||
                    std::abs(right - (dx + expectedRight * zoom)) > 0.001f ||
                    std::abs(bottom - (dy + expectedBottom * zoom)) > 0.001f) {
                    throw std::runtime_error(std::string("SVG arrow border contact failed: ") +
                                             arrow.id + (markerOnly ? " marker" : " shaft/marker"));
                }
            }
        }
    }

    void FillRect(const platform::Rect& r, const platform::Color& c) override {
        Rect("fillRect", r, 0, c, 0);
    }
    void FillRoundedRect(const platform::Rect& r, float radius, const platform::Color& c) override {
        Rect("fillRoundedRect", r, radius, c, 0);
    }
    void StrokeRect(const platform::Rect& r, const platform::Color& c, float width) override {
        Rect("strokeRect", r, 0, c, width);
    }
    void StrokeRoundedRect(const platform::Rect& r, float radius, const platform::Color& c,
                           float width) override {
        Rect("strokeRoundedRect", r, radius, c, width);
    }
    void StrokeLine(float x0, float y0, float x1, float y1, const platform::Color& c,
                    float width) override {
        auto out = Begin("line");
        if (c.r > 0.5f && c.g < 0.25f && c.b < 0.25f) {
            relationPoints.push_back({x0, y0});
            relationPoints.push_back({x1, y1});
        }
        out << ',' << x0 << ',' << y0 << ',' << x1 << ',' << y1;
        Color(out, c);
        out << ',' << width << ']';
        ops.push_back(out.str());
    }
    void DrawText(const std::string& text, const platform::FontSpec& font, float x,
                  float baseline, const platform::Color& c) override {
        auto out = Begin("text");
        out << ',' << Json(text) << ',' << Json(font.family) << ',' << font.size
            << ',' << font.bold << ',' << font.italic << ',' << font.monospace
            << ',' << x << ',' << baseline;
        Color(out, c);
        out << ']';
        ops.push_back(out.str());
    }
    void DrawBitmap(platform::BitmapHandle, const platform::Rect&) override {
        throw std::logic_error("SVG reference has no bitmap primitive");
    }
    void FillEllipse(const platform::Rect& r, const platform::Color& c) override {
        Rect("fillEllipse", r, 0, c, 0);
    }
    void StrokeEllipse(const platform::Rect& r, const platform::Color& c, float width) override {
        Rect("strokeEllipse", r, 0, c, width);
    }
    void FillPolygon(const std::vector<platform::Point>& points, const platform::Color& c) override {
        Polygon("fillPolygon", points, c, 0, true);
    }
    void StrokePolygon(const std::vector<platform::Point>& points, const platform::Color& c,
                       float width, bool closed) override {
        Polygon("strokePolygon", points, c, width, closed);
    }
    void FillPieSlice(const platform::Rect& r, float start, float end,
                      const platform::Color& c) override {
        auto out = Begin("pieSlice");
        out << ',' << r.x << ',' << r.y << ',' << r.width << ',' << r.height
            << ',' << start << ',' << end;
        Color(out, c);
        out << ']';
        ops.push_back(out.str());
    }
    void PushClip(const platform::Rect& r) override { Rect("pushClip", r, 0, {}, 0); }
    void PopClip() override { ops.push_back("[\"popClip\"]"); }
private:
    static std::ostringstream Begin(const char* name) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9) << '[' << Json(name);
        return out;
    }
    static void Color(std::ostringstream& out, const platform::Color& c) {
        out << ',' << c.r << ',' << c.g << ',' << c.b << ',' << c.a;
    }
    void Rect(const char* name, const platform::Rect& r, float radius,
              const platform::Color& c, float width) {
        auto out = Begin(name);
        out << ',' << r.x << ',' << r.y << ',' << r.width << ',' << r.height
            << ',' << radius;
        Color(out, c);
        out << ',' << width << ']';
        ops.push_back(out.str());
    }
    void Polygon(const char* name, const std::vector<platform::Point>& points,
                 const platform::Color& c, float width, bool closed) {
        auto out = Begin(name);
        const bool red = c.r > 0.5f && c.g < 0.25f && c.b < 0.25f;
        const bool green = c.g > 0.5f && c.r < 0.25f && c.b < 0.25f;
        if (red || green) {
            relationPoints.insert(relationPoints.end(), points.begin(), points.end());
        }
        if (green) markerPoints.insert(markerPoints.end(), points.begin(), points.end());
        out << ",[";
        bool first = true;
        for (const auto& p : points) {
            if (!first) out << ',';
            first = false;
            out << '[' << p.x << ',' << p.y << ']';
        }
        out << ']';
        Color(out, c);
        out << ',' << width << ',' << closed << ']';
        ops.push_back(out.str());
    }
};
#ifdef _WIN32
void CheckNative(HRESULT result) {
    if (SUCCEEDED(result)) return;
    std::ostringstream error;
    error << "Native SVG parity failed with HRESULT 0x" << std::hex
          << static_cast<unsigned long>(result);
    throw std::runtime_error(error.str());
}

struct ComApartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

// Run the actual Windows paint adapter into a private WARP target. The trace
// observes the adapter's calls, and EndDraw also checks native API execution.
class NativeRenderer {
public:
    NativeRenderer() {
        CheckNative(apartment_.status);
        CheckNative(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3d_.GetAddressOf(), nullptr, nullptr));
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        CheckNative(d3d_.As(&dxgi));
        Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
        CheckNative(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), nullptr,
            reinterpret_cast<void**>(factory.GetAddressOf())));
        Microsoft::WRL::ComPtr<ID2D1Device> device;
        CheckNative(factory->CreateDevice(dxgi.Get(), device.GetAddressOf()));
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> base;
        CheckNative(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                base.GetAddressOf()));
        CheckNative(base.As(&context_));
        CheckNative(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write_.GetAddressOf())));
        D2D1_BITMAP_PROPERTIES1 properties = {};
        properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                  D2D1_ALPHA_MODE_PREMULTIPLIED);
        properties.dpiX = properties.dpiY = 96.0f;
        properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        CheckNative(context_->CreateBitmap(D2D1::SizeU(1024, 768), nullptr, 0,
                                            &properties, bitmap_.GetAddressOf()));
        context_->SetDpi(96.0f, 96.0f);
        context_->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    }
    ID2D1DeviceContext5* Context() { return context_.Get(); }
    void Draw(svg::SvgDoc& doc, Commands& commands, FixedMetrics& metrics,
              float x, float y, float width, float height) {
        context_->SetTarget(bitmap_.Get());
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
        context_->BeginDraw();
        context_->Clear(D2D1::ColorF(D2D1::ColorF::White));
        doc.Draw(context_.Get(), write_.Get(), x, y, width, height, &commands, &metrics);
        const HRESULT result = context_->EndDraw();
        context_->SetTarget(nullptr);
        CheckNative(result);
    }
private:
    ComApartment apartment_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
};
#endif
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: svg-parity OUTPUT.json INPUT.svg [INPUT.svg ...]\n";
        return 2;
    }
    try {
#ifdef _WIN32
        NativeRenderer native;
#endif
        std::ofstream out(argv[1], std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create command trace");
        out << "{\"schema\":1,\"reference\":\"macOS\",\"metrics\":\"fixed-half-em\",\"cases\":[";
        bool first = true;
        std::size_t cases = 0, operations = 0;
        for (int i = 2; i < argc; ++i) {
            std::ifstream input(argv[i], std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open SVG fixture");
            const std::string xml((std::istreambuf_iterator<char>(input)), {});
#ifdef _WIN32
            svg::SvgDoc doc;
            if (!doc.Load(native.Context(), xml)) throw std::runtime_error("Cannot parse SVG fixture");
#else
            platform::SvgDoc doc;
            if (!doc.Load(xml)) throw std::runtime_error("Cannot parse SVG fixture");
#endif
            for (float zoom : {0.4f, 1.0f, 2.0f}) {
                for (int translated : {0, 1}) {
                    FixedMetrics metrics;
                    Commands target;
#ifdef _WIN32
                    native.Draw(doc, target, metrics, translated ? 67.0f : 0.0f,
                                translated ? 49.0f : 0.0f,
                                doc.Width() * zoom, doc.Height() * zoom);
#else
                    doc.Draw(&target, &metrics, translated ? 67.0f : 0.0f,
                             translated ? 49.0f : 0.0f,
                             doc.Width() * zoom, doc.Height() * zoom);
#endif
                    if (std::filesystem::path(argv[i]).filename() == "svg-arrow-contacts.svg") {
                        target.CheckArrowContacts(zoom, translated);
                    }
                    if (target.ops.empty()) throw std::runtime_error("Empty SVG scene");
                    if (!first) out << ',';
                    first = false;
                    out << "{\"file\":" << Json(std::filesystem::path(argv[i]).filename().string())
                        << ",\"zoom\":" << zoom << ",\"translated\":" << translated
                        << ",\"operations\":[";
                    for (std::size_t op = 0; op < target.ops.size(); ++op) {
                        if (op) out << ',';
                        out << target.ops[op];
                    }
                    out << "]}";
                    ++cases;
                    operations += target.ops.size();
                }
            }
        }
        out << "]}\n";
        out.close();
        if (!out) throw std::runtime_error("Cannot finish command trace");
        std::cout << cases << " cases, " << operations << " drawing commands\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
