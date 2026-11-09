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
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <utility>

#include "gtest_lite.h"
#include "svgdoc.h"

namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT32 kBitmapWidth = 1024;
constexpr UINT32 kBitmapHeight = 768;
constexpr float kDocWidth = 240.0f;
constexpr float kDocHeight = 140.0f;
constexpr char kSvg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="240" height="140" viewBox="0 0 240 140">
<rect x="20" y="20" width="200" height="100" fill="#40a0e0"/>
<text x="120" y="70" text-anchor="middle" dominant-baseline="central" font-family="Segoe UI" font-size="32" fill="#000000">HHHH</text>
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

    bool Load() { return document_.Load(context_.Get(), kSvg); }

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
        // No assertions or early returns while mapped: always unmap it.
        for (UINT32 py = 0; py < kBitmapHeight; ++py) {
            const BYTE* row = mapped.bits + static_cast<std::size_t>(py) * mapped.pitch;
            for (UINT32 px = 0; px < kBitmapWidth; ++px) {
                const BYTE* p = row + static_cast<std::size_t>(px) * 4;
                const int b = p[0], g = p[1], r = p[2];
                if (r < 80 && g < 80 && b < 80) {
                    result.text.Add(static_cast<int>(px), static_cast<int>(py));
                } else if (r < 110 && g > 110 && g < 200 && b > 170) {
                    result.shape.Add(static_cast<int>(px), static_cast<int>(py));
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
    // Central aligns the layout box, not the glyph's exact visual center.
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

#endif  // _WIN32
