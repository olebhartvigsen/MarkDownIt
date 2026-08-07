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

    // Monospace font for code blocks: Consolas 14pt.
    hr = dw->CreateTextFormat(
        L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f * kPtToDip,
        L"", &code_fmt_);
    if (FAILED(hr) || !code_fmt_) return false;

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
    rel(code_fmt_);
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

void Renderer::DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                              const Node& n, float x, float y,
                              float width, float& outH) {
    if (!rt || !dw || !code_fmt_) { outH = 0.0f; return; }

    // Convert raw text (UTF-32) to UTF-16 for DirectWrite.
    std::u16string text16 = ToUtf16(n.raw);

    // Draw a light gray background rounded rect.
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xF5F5F5), &bgBrush);
    ID2D1SolidColorBrush* codeBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x333333), &codeBrush);

    const float kCodePad = 10.0f;
    float codeWidth = width - 2.0f * kCodePad;
    if (codeWidth <= 0) { outH = 0.0f; if (bgBrush) bgBrush->Release(); if (codeBrush) codeBrush->Release(); return; }

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(text16.data()),
        static_cast<UINT32>(text16.size()),
        code_fmt_, codeWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) {
        if (bgBrush) bgBrush->Release();
        if (codeBrush) codeBrush->Release();
        outH = 0.0f;
        return;
    }

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);

    float blockH = metrics.height + 2.0f * kCodePad;

    // Background rect.
    D2D1_RECT_F bgRect = D2D1::RectF(x, y, x + width, y + blockH);
    rt->FillRectangle(bgRect, bgBrush);

    // Draw text.
    D2D1_POINT_2F origin = D2D1::Point2F(x + kCodePad, y + kCodePad);
    rt->DrawTextLayout(origin, layout, codeBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    layout->Release();
    bgBrush->Release();
    codeBrush->Release();
    outH = blockH;
}

void Renderer::DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y, float width) {
    ID2D1SolidColorBrush* lineBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xCCCCCC), &lineBrush);
    if (!lineBrush) return;
    D2D1_POINT_2F p1 = D2D1::Point2F(x, y);
    D2D1_POINT_2F p2 = D2D1::Point2F(x + width, y);
    rt->DrawLine(p1, p2, lineBrush, 1.0f);
    lineBrush->Release();
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
        float blockH = 0.0f;
        float drawX = kPadX;
        float drawW = contentWidth;

        if (n.block == BlockKind::BlockQuote) {
            drawX += 16.0f;
            drawW -= 16.0f;
        } else if (n.block == BlockKind::List) {
            drawX += 24.0f;
            drawW -= 24.0f;
        }

        if (n.block == BlockKind::CodeBlock) {
            std::u16string text16 = ToUtf16(n.raw);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                code_fmt_, drawW - 20.0f, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                DWRITE_TEXT_METRICS m = {};
                layout->GetMetrics(&m);
                blockH = m.height + 20.0f;
                layout->Release();
            }
        } else if (n.block == BlockKind::ThematicBreak) {
            blockH = 12.0f;
        } else {
            IDWriteTextFormat* fmt = nullptr;
            if (n.block == BlockKind::Heading) {
                int lvl = n.level; if (lvl < 1) lvl = 1; if (lvl > 6) lvl = 6;
                fmt = heading_fmt_[lvl];
            } else if (n.block == BlockKind::Paragraph ||
                       n.block == BlockKind::BlockQuote ||
                       n.block == BlockKind::List) {
                fmt = body_fmt_;
            }
            if (fmt) {
                std::u32string text32;
                float markerW = 0.0f;
                // For list items, account for the marker width offset.
                if (n.block == BlockKind::List) {
                    markerW = 24.0f;
                    text32 += n.ordered ? U"1. " : U"\u2022  ";
                }
                for (const auto& ib : n.children) text32 += ib.text;
                if (text32.empty()) { curY += kBlockGap; continue; }
                std::u16string text16 = ToUtf16(text32);
                IDWriteTextLayout* layout = nullptr;
                float layoutW = drawW - markerW;
                HRESULT hr = dw->CreateTextLayout(
                    reinterpret_cast<const WCHAR*>(text16.data()),
                    static_cast<UINT32>(text16.size()),
                    fmt, layoutW > 0 ? layoutW : drawW, 1.0e9f, &layout);
                if (SUCCEEDED(hr) && layout) {
                    DWRITE_TEXT_METRICS m = {};
                    layout->GetMetrics(&m);
                    blockH = m.height;
                    layout->Release();
                }
                // H1 gets an HR line underneath: add 5 DIPs.
                if (n.block == BlockKind::Heading && n.level == 1) {
                    blockH += 5.0f;
                }
            }
        }

        curY += blockH + kBlockGap;
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

    // Quote border brush.
    ID2D1SolidColorBrush* quoteBorder = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x999999), &quoteBorder);

    // Apply scroll translation.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -scrollY));

    float curY = kPadTop;
    int listCounter = 0;

    for (const Node& n : doc.nodes) {
        float drawX = kPadX;
        float drawW = contentWidth;

        if (n.block == BlockKind::BlockQuote) {
            drawX += 16.0f;
            drawW -= 16.0f;
        } else if (n.block == BlockKind::List) {
            drawX += 24.0f;
            drawW -= 24.0f;
        }

        if (n.block == BlockKind::CodeBlock) {
            float blockH = 0.0f;
            DrawCodeBlock(rt, dw, n, drawX, curY, drawW, blockH);
            curY += blockH + kBlockGap;
            continue;
        }

        if (n.block == BlockKind::ThematicBreak) {
            DrawThematicBreak(rt, drawX, curY + 6.0f, drawW);
            curY += 12.0f + kBlockGap;
            continue;
        }

        // Heading, Paragraph, BlockQuote, List: all use body or heading fmt.
        IDWriteTextFormat* fmt = nullptr;
        if (n.block == BlockKind::Heading) {
            int lvl = n.level; if (lvl < 1) lvl = 1; if (lvl > 6) lvl = 6;
            fmt = heading_fmt_[lvl];
        } else if (n.block == BlockKind::Paragraph ||
                   n.block == BlockKind::BlockQuote ||
                   n.block == BlockKind::List) {
            fmt = body_fmt_;
        }

        if (!fmt) { continue; }

        // For list items: draw the marker separately so wrapped lines
        // align with the text, not the marker.
        float markerW = 0.0f;
        std::u16string marker16;
        if (n.block == BlockKind::List) {
            const float kMarkerW = 24.0f;
            markerW = kMarkerW;
            if (n.ordered) {
                ++listCounter;
                char buf[16];
                snprintf(buf, sizeof(buf), "%d. ", listCounter);
                for (const char* p = buf; *p; ++p)
                    marker16.push_back(static_cast<char16_t>(*p));
            } else {
                marker16 += u"\u2022 ";
            }
        } else {
            listCounter = 0;
        }

        for (const auto& ib : n.children) {
            text32 += ib.text;
        }
        if (text32.empty() && marker16.empty()) {
            curY += kBlockGap;
            continue;
        }

        std::u16string text16 = ToUtf16(text32);

        // Text starts after the marker for list items.
        float textX = drawX + markerW;
        float textW = drawW - markerW;

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(text16.data()),
            static_cast<UINT32>(text16.size()),
            fmt, textW > 0 ? textW : drawW, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) continue;

        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);

        // Draw marker first (for list items).
        if (!marker16.empty()) {
            IDWriteTextLayout* markerLayout = nullptr;
            hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(marker16.data()),
                static_cast<UINT32>(marker16.size()),
                fmt, markerW, 1.0e9f, &markerLayout);
            if (SUCCEEDED(hr) && markerLayout) {
                D2D1_POINT_2F mOrigin = D2D1::Point2F(drawX, curY);
                rt->DrawTextLayout(mOrigin, markerLayout, black,
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                markerLayout->Release();
            }
        }

        D2D1_POINT_2F origin = D2D1::Point2F(textX, curY);
        rt->DrawTextLayout(origin, layout, black, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Draw quote border on the left.
        if (n.block == BlockKind::BlockQuote && quoteBorder) {
            D2D1_POINT_2F p1 = D2D1::Point2F(kPadX + 4.0f, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(kPadX + 4.0f, curY + metrics.height);
            rt->DrawLine(p1, p2, quoteBorder, 3.0f);
        }

        // Draw HR line under H1 headings.
        if (n.block == BlockKind::Heading && n.level == 1) {
            ID2D1SolidColorBrush* hrBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(0xDDDDDD), &hrBrush);
            if (hrBrush) {
                D2D1_POINT_2F p1 = D2D1::Point2F(drawX, curY + metrics.height + 4.0f);
                D2D1_POINT_2F p2 = D2D1::Point2F(drawX + drawW, curY + metrics.height + 4.0f);
                rt->DrawLine(p1, p2, hrBrush, 1.0f);
                hrBrush->Release();
            }
            curY += 5.0f;  // extra for HR line
        }

        layout->Release();
        curY += metrics.height + kBlockGap;
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    if (black) black->Release();
    if (quoteBorder) quoteBorder->Release();
    return curY;
}
