#include "renderer.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>

Renderer::Renderer() {}
Renderer::~Renderer() { Release(); }

bool Renderer::Init(IDWriteFactory* dw) {
    if (!dw) return false;

    const float kPtToDip = 96.0f / 72.0f;

    HRESULT hr = dw->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        16.0f * kPtToDip,
        L"", &body_fmt_);
    if (FAILED(hr) || !body_fmt_) return false;

    const float sizes[7] = {0.0f, 24.0f, 22.0f, 20.0f, 18.0f, 16.0f, 16.0f};
    for (int lvl = 1; lvl <= 6; ++lvl) {
        hr = dw->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            sizes[lvl] * kPtToDip,
            L"", &heading_fmt_[lvl]);
        if (FAILED(hr) || !heading_fmt_[lvl]) return false;
    }

    return true;
}

void Renderer::Release() {
    auto rel = [](IDWriteTextFormat*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(body_fmt_);
    for (int i = 1; i <= 6; ++i) rel(heading_fmt_[i]);
}

std::u16string Renderer::ToUtf16(const std::u32string& s32) {
    std::u16string out;
    out.reserve(s32.size());
    for (char32_t cp : s32) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}


float Renderer::Measure(IDWriteFactory* dw, const Document& doc, float widthDip) {
    if (!dw) return 0.0f;
    const float kPadX = 24.0f;
    const float kPadTop = 24.0f;
    const float kBlockGap = 12.0f;

    if (widthDip <= 2.0f * kPadX) return kPadTop;
    float contentWidth = widthDip - 2.0f * kPadX;

    float curY = kPadTop;
    for (const Node& n : doc.nodes) {
        IDWriteTextFormat* fmt = nullptr;
        if (n.block == BlockKind::Heading) {
            int lvl = n.level; if (lvl < 1) lvl = 1; if (lvl > 6) lvl = 6;
            fmt = heading_fmt_[lvl];
        } else if (n.block == BlockKind::Paragraph) {
            fmt = body_fmt_;
        }
        if (!fmt) continue;

        std::u32string text32;
        for (const auto& ib : n.children) text32 += ib.text;
        if (text32.empty()) { curY += kBlockGap; continue; }
        std::u16string text16 = ToUtf16(text32);

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(text16.data()),
            static_cast<UINT32>(text16.size()),
            fmt, contentWidth, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) continue;

        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        layout->Release();
        curY += metrics.height + kBlockGap;
    }
    return curY;
}

float Renderer::Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Document& doc, float widthDip, float scrollY) {
    if (!rt || !dw) return 0.0f;
    const float kPadX = 24.0f;
    const float kPadTop = 24.0f;
    const float kBlockGap = 12.0f;

    if (widthDip <= 2.0f * kPadX) return kPadTop;
    float contentWidth = widthDip - 2.0f * kPadX;

    ID2D1SolidColorBrush* black = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &black);
    if (!black) return kPadTop;

    // Apply scroll translation so content moves up as scrollY increases.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -scrollY));

    float curY = kPadTop;

    for (const Node& n : doc.nodes) {
        IDWriteTextFormat* fmt = nullptr;
        if (n.block == BlockKind::Heading) {
            int lvl = n.level;
            if (lvl < 1) lvl = 1;
            if (lvl > 6) lvl = 6;
            fmt = heading_fmt_[lvl];
        } else if (n.block == BlockKind::Paragraph) {
            fmt = body_fmt_;
        }
        if (!fmt) continue;

        std::u32string text32;
        for (const auto& ib : n.children) {
            text32 += ib.text;
        }
        if (text32.empty()) {
            curY += kBlockGap;
            continue;
        }
        std::u16string text16 = ToUtf16(text32);

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(text16.data()),
            static_cast<UINT32>(text16.size()),
            fmt, contentWidth, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) continue;

        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);

        D2D1_POINT_2F origin = D2D1::Point2F(kPadX, curY);
        rt->DrawTextLayout(origin, layout, black, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        layout->Release();
        curY += metrics.height + kBlockGap;
    }

    // Reset transform before we return.
    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    black->Release();
    return curY;
}
