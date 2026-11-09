// Native SVG shapes and DirectWrite text must share one document transform.
// This renders into a private WARP bitmap, never a window or a saved image.
#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gtest_lite.h"
#include "svgdoc.h"
#include "svg_native_fixture.h"

namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT32 kBitmapWidth = 1024;
constexpr UINT32 kBitmapHeight = 768;
constexpr float kDocWidth = 240.0f;
constexpr float kDocHeight = 140.0f;
constexpr char kSvg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="240" height="140" viewBox="0 0 240 140">
<rect x="20" y="20" width="200" height="100" fill="#40a0e0"/>
<text x="120" y="82" text-anchor="middle" font-family="Segoe UI" font-size="32" fill="#000000">HHHH</text>
</svg>)svg";

// Keep HRESULT failures diagnostic and fatal to the case, not silent skips.
#define SVG_RETURN_IF_FAILED(expression) do { \
    const HRESULT svgHr = (expression); \
    if (FAILED(svgHr)) { \
        std::cerr << "  " #expression " failed: 0x" << std::hex \
                  << static_cast<unsigned long>(svgHr) << std::dec << '\n'; \
        return svgHr; \
    } \
} while (0)

struct ComApartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

// Pixel bounds use half-open edges, so width/height scale normally.
struct InkBounds {
    int left = static_cast<int>(kBitmapWidth);
    int top = static_cast<int>(kBitmapHeight);
    int right = 0;
    int bottom = 0;
    std::size_t pixels = 0;

    void Add(int x, int y) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + 1);
        bottom = std::max(bottom, y + 1);
        ++pixels;
    }
};

struct RenderResult {
    InkBounds shape;
    InkBounds text;
    InkBounds redText;
    std::vector<unsigned int> pixels;
    D2D1_MATRIX_3X2_F restored = {};
};

class OffscreenSvg {
public:
    HRESULT Initialize() {
        SVG_RETURN_IF_FAILED(apartment_.hr);
        // Software rendering works on CI without a display or physical GPU.
        SVG_RETURN_IF_FAILED(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3d_.GetAddressOf(), nullptr, nullptr));
        SVG_RETURN_IF_FAILED(d3d_.As(&dxgi_));
        SVG_RETURN_IF_FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
            nullptr, reinterpret_cast<void**>(factory_.GetAddressOf())));
        SVG_RETURN_IF_FAILED(factory_->CreateDevice(
            dxgi_.Get(), device_.GetAddressOf()));
        ComPtr<ID2D1DeviceContext> baseContext;
        SVG_RETURN_IF_FAILED(device_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, baseContext.GetAddressOf()));
        SVG_RETURN_IF_FAILED(baseContext.As(&context_));
        SVG_RETURN_IF_FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_.GetAddressOf())));

        D2D1_BITMAP_PROPERTIES1 properties = {};
        properties.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        properties.dpiX = properties.dpiY = 96.0f;
        properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        const D2D1_SIZE_U size = D2D1::SizeU(kBitmapWidth, kBitmapHeight);
        SVG_RETURN_IF_FAILED(context_->CreateBitmap(
            size, nullptr, 0, &properties, target_.GetAddressOf()));
        // CPU_READ requires CANNOT_DRAW and cannot be combined with TARGET.
        properties.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
        SVG_RETURN_IF_FAILED(context_->CreateBitmap(
            size, nullptr, 0, &properties, readback_.GetAddressOf()));
        context_->SetDpi(96.0f, 96.0f);
        context_->SetUnitMode(D2D1_UNIT_MODE_DIPS);
        context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return S_OK;
    }

    bool Load(const std::string& xml = kSvg) { return document_.Load(context_.Get(), xml); }

    HRESULT Render(float zoom, float x, float y,
                   const D2D1_MATRIX_3X2_F& outer, RenderResult& result) {
        result = {};
        context_->SetTarget(target_.Get());
        context_->SetTransform(outer);
        context_->BeginDraw();
        context_->Clear(D2D1::ColorF(D2D1::ColorF::White));
        document_.Draw(context_.Get(), write_.Get(), x, y,
                       kDocWidth * zoom, kDocHeight * zoom);
        context_->GetTransform(&result.restored);
        // EndDraw must succeed before any pixel assertions are meaningful.
        const HRESULT drawHr = context_->EndDraw();
        context_->SetTarget(nullptr);
        SVG_RETURN_IF_FAILED(drawHr);
        SVG_RETURN_IF_FAILED(readback_->CopyFromBitmap(
            nullptr, target_.Get(), nullptr));
        D2D1_MAPPED_RECT mapped = {};
        SVG_RETURN_IF_FAILED(readback_->Map(D2D1_MAP_OPTIONS_READ, &mapped));
        result.pixels.resize(static_cast<std::size_t>(kBitmapWidth) * kBitmapHeight);
        // No assertions or early returns while mapped: always unmap it.
        for (UINT32 py = 0; py < kBitmapHeight; ++py) {
            const BYTE* row = mapped.bits + static_cast<std::size_t>(py) * mapped.pitch;
            for (UINT32 px = 0; px < kBitmapWidth; ++px) {
                const BYTE* p = row + static_cast<std::size_t>(px) * 4;
                const int b = p[0], g = p[1], r = p[2];
                result.pixels[static_cast<std::size_t>(py) * kBitmapWidth + px] =
                    static_cast<unsigned int>((r << 16) | (g << 8) | b);
                if (r < 80 && g < 80 && b < 80) {
                    result.text.Add(static_cast<int>(px), static_cast<int>(py));
                } else if (r < 110 && g > 110 && g < 200 && b > 170) {
                    result.shape.Add(static_cast<int>(px), static_cast<int>(py));
                } else if (r > 160 && g < 80 && b < 80) {
                    result.redText.Add(static_cast<int>(px), static_cast<int>(py));
                }
            }
        }
        SVG_RETURN_IF_FAILED(readback_->Unmap());
        return S_OK;
    }

private:
    // COM apartment outlives every COM reference, including the SVG document.
    ComApartment apartment_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice> dxgi_;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<ID2D1Device> device_;
    ComPtr<ID2D1DeviceContext5> context_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1Bitmap1> readback_;
    svg::SvgDoc document_;
};

#undef SVG_RETURN_IF_FAILED

void ExpectRestored(const D2D1_MATRIX_3X2_F& actual,
                    const D2D1_MATRIX_3X2_F& expected) {
    EXPECT_EQ(actual._11, expected._11);
    EXPECT_EQ(actual._12, expected._12);
    EXPECT_EQ(actual._21, expected._21);
    EXPECT_EQ(actual._22, expected._22);
    EXPECT_EQ(actual._31, expected._31);
    EXPECT_EQ(actual._32, expected._32);
}

void ExpectBounds(const InkBounds& actual, float left, float top,
                  float right, float bottom, float tolerance) {
    EXPECT_NEAR(actual.left, left, tolerance);
    EXPECT_NEAR(actual.top, top, tolerance);
    EXPECT_NEAR(actual.right, right, tolerance);
    EXPECT_NEAR(actual.bottom, bottom, tolerance);
}

void CheckAlignment(float zoom, const D2D1_MATRIX_3X2_F& outer) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    ASSERT_TRUE(renderer.Load());

    // Measure real glyph ink at unit scale instead of assuming font metrics
    // or a golden raster shared by every Windows font/rasterizer version.
    RenderResult reference;
    const auto identity = D2D1::Matrix3x2F::Identity();
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1.0f, 0, 0, identity, reference)));
    ASSERT_TRUE(reference.shape.pixels > 0);
    ASSERT_TRUE(reference.text.pixels > 0);
    ExpectRestored(reference.restored, identity);
    ExpectBounds(reference.shape, 20, 20, 220, 120, 1.0f);
    EXPECT_NEAR((reference.text.left + reference.text.right) / 2.0f, 120, 3.0f);
    // The reference renderer uses an SVG baseline. At baseline 82 the
    // uppercase ink sits near the shape's vertical center at 70.
    EXPECT_NEAR((reference.text.top + reference.text.bottom) / 2.0f, 70, 10.0f);

    constexpr float x = 67.0f;
    constexpr float y = 49.0f;
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(zoom, x, y, outer, actual)));
    ExpectRestored(actual.restored, outer);
    ASSERT_TRUE(actual.shape.pixels > 0);
    ASSERT_TRUE(actual.text.pixels > 0);

    // Independent scalar oracle for row-vector order: document placement
    // happens first, then the caller's scale and scroll translation. These
    // cases use positive diagonal outer matrices, so bounds stay axis-aligned.
    const auto mapX = [&](float p) { return (x + zoom * p) * outer._11 + outer._31; };
    const auto mapY = [&](float p) { return (y + zoom * p) * outer._22 + outer._32; };
    ExpectBounds(actual.shape, mapX(20), mapY(20), mapX(220), mapY(120), 1.5f);
    // Antialias thresholds and hinting can move ink edges by a few pixels.
    // All four edges check scale as well as position, including at 2x zoom.
    ExpectBounds(actual.text,
                 mapX(static_cast<float>(reference.text.left)),
                 mapY(static_cast<float>(reference.text.top)),
                 mapX(static_cast<float>(reference.text.right)),
                 mapY(static_cast<float>(reference.text.bottom)), 4.0f);
    EXPECT_GE(actual.text.left, actual.shape.left);
    EXPECT_GE(actual.text.top, actual.shape.top);
    EXPECT_LE(actual.text.right, actual.shape.right);
    EXPECT_LE(actual.text.bottom, actual.shape.bottom);
    const float shapeWidth = static_cast<float>(actual.shape.right - actual.shape.left);
    const float shapeHeight = static_cast<float>(actual.shape.bottom - actual.shape.top);
    const float centerX = (actual.text.left + actual.text.right) / 2.0f;
    const float centerY = (actual.text.top + actual.text.bottom) / 2.0f;
    EXPECT_NEAR((centerX - actual.shape.left) / shapeWidth, 0.5f, 0.04f);
    EXPECT_NEAR((centerY - actual.shape.top) / shapeHeight, 0.5f, 0.15f);
}

D2D1_MATRIX_3X2_F ScrolledOuter() {
    // Nonuniform scale and negative scroll make reversed composition visible.
    return D2D1::Matrix3x2F(1.25f, 0, 0, 0.9f, 31.0f, -13.0f);
}

}  // namespace

TEST(SvgDocTransform, ZoomOutShapeAndText) {
    CheckAlignment(0.4f, D2D1::Matrix3x2F::Identity());
}

TEST(SvgDocTransform, UnitZoomTranslatedShapeAndText) {
    CheckAlignment(1.0f, D2D1::Matrix3x2F::Identity());
}

TEST(SvgDocTransform, ZoomInShapeAndText) {
    CheckAlignment(2.0f, D2D1::Matrix3x2F::Identity());
}

TEST(SvgDocTransform, ZoomOutWithOuterTransformRestored) {
    CheckAlignment(0.4f, ScrolledOuter());
}

TEST(SvgDocTransform, UnitZoomWithOuterTransformRestored) {
    CheckAlignment(1.0f, ScrolledOuter());
}

TEST(SvgDocTransform, ZoomInWithOuterTransformRestored) {
    CheckAlignment(2.0f, ScrolledOuter());
}

TEST(SvgDocReference, LaterOpaqueShapeCoversEarlierText) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    ASSERT_TRUE(renderer.Load(
        "<svg width='240' height='140'>"
        "<text x='120' y='82' text-anchor='middle' font-size='32'>HHHH</text>"
        "<rect x='0' y='0' width='240' height='140' fill='#40a0e0'/>"
        "</svg>"));
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 0, 0, D2D1::Matrix3x2F::Identity(), actual)));
    EXPECT_TRUE(actual.shape.pixels > 0);
    EXPECT_EQ(actual.text.pixels, 0u);
}

TEST(SvgDocReference, ViewBoxOffsetAppliesToBothShapesAndText) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    ASSERT_TRUE(renderer.Load(
        "<svg width='240' height='140' viewBox='10 20 240 140'>"
        "<rect x='30' y='40' width='200' height='100' fill='#40a0e0'/>"
        "<text x='130' y='102' text-anchor='middle' font-family='Segoe UI'"
        " font-size='32'>HHHH</text></svg>"));
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 67, 49, D2D1::Matrix3x2F::Identity(), actual)));
    ASSERT_TRUE(actual.shape.pixels > 0);
    ASSERT_TRUE(actual.text.pixels > 0);
    ExpectBounds(actual.shape, 87, 69, 287, 169, 1.0f);
    EXPECT_NEAR((actual.text.left + actual.text.right) / 2.0f, 187, 3.0f);
    EXPECT_NEAR((actual.text.top + actual.text.bottom) / 2.0f, 119, 10.0f);
}

TEST(SvgDocReference, NearbyBoxDoesNotMoveStartAnchoredText) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    ASSERT_TRUE(renderer.Load(
        "<svg width='240' height='140'><g data='label'>"
        "<rect x='20' y='20' width='200' height='100' fill='#40a0e0'/>"
        "<text x='40' y='50' font-family='Segoe UI' font-size='32'>H</text>"
        "</g></svg>"));
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 0, 0, D2D1::Matrix3x2F::Identity(), actual)));
    ASSERT_TRUE(actual.text.pixels > 0);
    EXPECT_GE(actual.text.left, 40);
    EXPECT_LE(actual.text.left, 46);
    EXPECT_LE(actual.text.bottom, 51);
}

void CheckReferenceTextColor(const char* fill) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    const std::string xml = std::string("<svg width='240' height='140'>") +
        "<text x='40' y='70' font-size='32' fill='" + fill + "'>HHHH</text></svg>";
    ASSERT_TRUE(renderer.Load(xml));
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 0, 0, D2D1::Matrix3x2F::Identity(), actual)));
    EXPECT_TRUE(actual.redText.pixels > 0);
    EXPECT_EQ(actual.text.pixels, 0u);
}

TEST(SvgDocReference, ShortHexTextColorMatchesMac) {
    CheckReferenceTextColor("#f00");
}

TEST(SvgDocReference, RgbTextColorMatchesMac) {
    CheckReferenceTextColor("rgb(255,0,0)");
}

TEST(SvgDocReference, NativePrimitiveAndSingleLineProbes) {
    OffscreenSvg renderer;
    ASSERT_TRUE(SUCCEEDED(renderer.Initialize()));
    ASSERT_TRUE(renderer.Load(svg_native_fixture::kSvg));
    RenderResult actual;
    ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 0, 0, D2D1::Matrix3x2F::Identity(), actual)));
    std::vector<std::array<int, 3>> colors;
    for (const auto& probe : svg_native_fixture::kProbes) {
        const auto color = actual.pixels[static_cast<std::size_t>(probe.y) * kBitmapWidth + probe.x];
        const std::array<int, 3> rgb = {
            static_cast<int>((color >> 16) & 255),
            static_cast<int>((color >> 8) & 255),
            static_cast<int>(color & 255)};
        colors.push_back(rgb);
        if (std::abs(rgb[0] - probe.r) > 2 || std::abs(rgb[1] - probe.g) > 2 ||
            std::abs(rgb[2] - probe.b) > 2) std::cerr << "Native probe: " << probe.name << '\n';
        EXPECT_NEAR(rgb[0], probe.r, 2);
        EXPECT_NEAR(rgb[1], probe.g, 2);
        EXPECT_NEAR(rgb[2], probe.b, 2);
    }
    const char* ids[] = {"plain", "newline", "anchored-newline"};
    const char* texts[] = {"H", "H&#10;HH", "H&#10;HHHH"};
    std::vector<InkBounds> bounds;
    for (int i = 0; i < 3; ++i) {
        const std::string xml = std::string("<svg width='240' height='140'>") +
            "<text x='" + (i == 2 ? "120" : "30") +
            "' y='130' font-family='Arial' font-size='24'" +
            (i == 2 ? " text-anchor='middle'" : "") + ">" + texts[i] + "</text></svg>";
        ASSERT_TRUE(renderer.Load(xml));
        ASSERT_TRUE(SUCCEEDED(renderer.Render(1, 0, 0, D2D1::Matrix3x2F::Identity(), actual)));
        // Use the same threshold as the Core Graphics reference. The older
        // alignment cases deliberately keep their stricter ink classifier.
        InkBounds nativeText;
        for (UINT32 py = 0; py < kBitmapHeight; ++py) {
            for (UINT32 px = 0; px < kBitmapWidth; ++px) {
                const auto color = actual.pixels[static_cast<std::size_t>(py) * kBitmapWidth + px];
                if (((color >> 16) & 255) < 128 && ((color >> 8) & 255) < 128 &&
                    (color & 255) < 128) nativeText.Add(static_cast<int>(px), static_cast<int>(py));
            }
        }
        ASSERT_TRUE(nativeText.pixels > 0);
        EXPECT_LE(nativeText.bottom - nativeText.top, 26);
        EXPECT_LE(nativeText.bottom, 131);
        bounds.push_back(nativeText);
    }
    if (const char* path = std::getenv("MDI_SVG_NATIVE_OUT")) {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.good());
        output << "{\"schema\":1,\"probes\":[";
        for (std::size_t i = 0; i < colors.size(); ++i) {
            if (i) output << ',';
            output << "{\"name\":\"" << svg_native_fixture::kProbes[i].name
                   << "\",\"rgb\":[" << colors[i][0] << ',' << colors[i][1] << ',' << colors[i][2] << "]}";
        }
        output << "],\"text\":[";
        for (std::size_t i = 0; i < bounds.size(); ++i) {
            if (i) output << ',';
            output << "{\"id\":\"" << ids[i] << "\",\"bounds\":[" << bounds[i].left << ','
                   << bounds[i].top << ',' << bounds[i].right << ',' << bounds[i].bottom << "]}";
        }
        output << "]}\n";
        output.close();
        EXPECT_TRUE(output.good());
    }
}

#endif  // _WIN32
