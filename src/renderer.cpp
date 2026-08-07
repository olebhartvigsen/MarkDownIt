#include "renderer.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "colortext.h"
#include "imagehelper.h"

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
        14.0f * kPtToDip,
        L"", &body_fmt_);
    if (FAILED(hr) || !body_fmt_) return false;

    // Monospace font for code blocks: Consolas 14pt.
    hr = dw->CreateTextFormat(
        L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f * kPtToDip,
        L"", &code_fmt_);
    if (FAILED(hr) || !code_fmt_) return false;

    const float sizes[7] = {0.0f, 20.0f, 18.0f, 16.0f, 15.0f, 14.0f, 14.0f};
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
    // Strip trailing newlines (md4c appends 
 at end of code blocks).
    std::u32string raw = n.raw;
    while (!raw.empty() && (raw.back() == U'
' || raw.back() == U'')) {
        raw.pop_back();
    }
    std::u16string text16 = ToUtf16(raw);

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

void Renderer::DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                          const Node& n, float x, float y,
                          float width, float& outH) {
    if (!rt || !dw || !body_fmt_ || n.rows.empty()) { outH = 0.0f; return; }

    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &textBrush);
    ID2D1SolidColorBrush* headerBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xE8E8E8), &headerBg);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xCCCCCC), &borderBrush);

    size_t cols = 0;
    for (const auto& row : n.rows) {
        cols = (row.cells.size() > cols) ? row.cells.size() : cols;
    }
    if (cols == 0) { outH = 0.0f; if (textBrush) textBrush->Release(); if (headerBg) headerBg->Release(); if (borderBrush) borderBrush->Release(); return; }

    float colW = width / static_cast<float>(cols);
    const float kCellPad = 8.0f;
    float curY = y;

    for (const auto& row : n.rows) {
        float rowH = 0.0f;
        // Measure each cell to find the tallest one.
        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colW - 2.0f * kCellPad, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                DWRITE_TEXT_METRICS m = {};
                layout->GetMetrics(&m);
                if (m.height > rowH) rowH = m.height;
                layout->Release();
            }
        }
        if (rowH == 0.0f) rowH = 20.0f;
        rowH += 2.0f * kCellPad;

        // Header background.
        bool isHeaderRow = (!row.cells.empty() && row.cells[0].isHeader);
        if (isHeaderRow && headerBg) {
            D2D1_RECT_F bg = D2D1::RectF(x, curY, x + width, curY + rowH);
            rt->FillRectangle(bg, headerBg);
        }

        // Draw cells.
        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            float cellX = x + static_cast<float>(c) * colW;
            // Border.
            if (borderBrush) {
                D2D1_POINT_2F p1 = D2D1::Point2F(cellX, curY);
                D2D1_POINT_2F p2 = D2D1::Point2F(cellX, curY + rowH);
                rt->DrawLine(p1, p2, borderBrush, 1.0f);
            }
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colW - 2.0f * kCellPad, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0, static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                }
                D2D1_POINT_2F origin = D2D1::Point2F(cellX + kCellPad, curY + kCellPad);
                rt->DrawTextLayout(origin, layout, textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                layout->Release();
            }
        }
        // Right border.
        if (borderBrush) {
            D2D1_POINT_2F p1 = D2D1::Point2F(x + width, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(x + width, curY + rowH);
            rt->DrawLine(p1, p2, borderBrush, 1.0f);
        }
        // Top and bottom border.
        if (borderBrush) {
            rt->DrawLine(D2D1::Point2F(x, curY), D2D1::Point2F(x + width, curY), borderBrush, 1.0f);
            rt->DrawLine(D2D1::Point2F(x, curY + rowH), D2D1::Point2F(x + width, curY + rowH), borderBrush, 1.0f);
        }

        curY += rowH;
    }

    if (textBrush) textBrush->Release();
    if (headerBg) headerBg->Release();
    if (borderBrush) borderBrush->Release();
    outH = curY - y;
}

float Renderer::Measure(IDWriteFactory* dw, const Document& doc, float widthDip) {
    if (!dw) return 0.0f;
    const float kPadX = 48.0f;
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
            float quoteIndent = 16.0f * (n.depth + 1);
            drawX += quoteIndent;
            drawW -= quoteIndent;
        } else if (n.block == BlockKind::List) {
            float listIndent = 24.0f * (n.depth + 1);
            drawX += listIndent;
            drawW -= listIndent;
        }

        if (n.block == BlockKind::CodeBlock) {
            std::u32string raw = n.raw;
            while (!raw.empty() && (raw.back() == U'
' || raw.back() == U'')) {
                raw.pop_back();
            }
            std::u16string text16 = ToUtf16(raw);
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
        } else if (n.block == BlockKind::Table) {
            // Estimate table height: count rows * 28px per row (approx).
            blockH = static_cast<float>(n.rows.size()) * 28.0f;
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
                if (n.block == BlockKind::Heading && n.level <= 2) {
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
    const float kPadX = 48.0f;
    const float kPadTop = 24.0f;
    const float kBlockGap = 12.0f;
    if (widthDip <= 2.0f * kPadX) return kPadTop;
    float contentWidth = widthDip - 2.0f * kPadX;

    ID2D1SolidColorBrush* black = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &black);
    if (!black) return kPadTop;

    // Link brush (blue).
    ID2D1SolidColorBrush* linkBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x0000CC), &linkBrush);

    // Quote border brush.
    ID2D1SolidColorBrush* quoteBorder = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x999999), &quoteBorder);

    // Apply scroll translation.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -scrollY));

    float curY = kPadTop;
    int listCounter = 0;
    BlockKind prevBlock = BlockKind::Paragraph;
    int prevDepth = -1;

    for (const Node& n : doc.nodes) {
        float drawX = kPadX;
        float drawW = contentWidth;

        if (n.block == BlockKind::BlockQuote) {
            float quoteIndent = 16.0f * (n.depth + 1);
            drawX += quoteIndent;
            drawW -= quoteIndent;
        } else if (n.block == BlockKind::List) {
            float listIndent = 24.0f * (n.depth + 1);
            drawX += listIndent;
            drawW -= listIndent;
        }

        if (n.block == BlockKind::CodeBlock) {
            float blockH = 0.0f;
            DrawCodeBlock(rt, dw, n, drawX, curY, drawW, blockH);
            curY += blockH + kBlockGap;
            continue;
        }

        if (n.block == BlockKind::Table) {
            float blockH = 0.0f;
            DrawTable(rt, dw, n, drawX, curY, drawW, blockH);
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

        std::u32string text32;

        // For list items: draw the marker separately so wrapped lines
        // align with the text, not the marker.
        float markerW = 0.0f;
        std::u16string marker16;
        if (n.block == BlockKind::List) {
            const float kMarkerW = 24.0f;
            markerW = kMarkerW;
            // Reset counter when starting a new list or changing depth.
            if (prevBlock != BlockKind::List || n.depth != prevDepth) {
                listCounter = 0;
            }
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

        // Build full text and track each inline block's range for styling.
        // We store the UTF-16 start position and length of each inline.
        struct SpanRange {
            UINT32 start;
            UINT32 length;
            bool em;
            bool strong;
            bool code;
            bool link;
            bool strike;
        };
        std::u16string text16;
        std::vector<SpanRange> spans;

        for (const auto& ib : n.children) {
            // Skip image alt text (images are drawn separately above).
            if (ib.kind == InlineKind::Image) continue;
            // Convert this inline block's text to UTF-16 and record its range.
            std::u16string part16 = ToUtf16(ib.text);
            UINT32 start = static_cast<UINT32>(text16.size());
            text16 += part16;
            UINT32 length = static_cast<UINT32>(part16.size());
            if (length > 0) {
                spans.push_back({start, length, ib.em, ib.strong, ib.code,
                                 ib.kind == InlineKind::Link, ib.strike});
            }
        }

        if (text16.empty() && marker16.empty()) {
            curY += kBlockGap;
            continue;
        }

        // Text starts after the marker for list items.
        float textX = drawX + markerW;
        float textW = drawW - markerW;

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(text16.data()),
            static_cast<UINT32>(text16.size()),
            fmt, textW > 0 ? textW : drawW, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) continue;

        // Apply inline span styling via DWRITE_TEXT_RANGE.
        for (const auto& s : spans) {
            DWRITE_TEXT_RANGE range = {s.start, s.length};
            if (s.strong) {
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            }
            if (s.em) {
                layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            }
            if (s.code) {
                // Switch to monospace font for inline code spans.
                layout->SetFontFamilyName(L"Consolas", range);
                layout->SetFontSize(14.0f * (96.0f / 72.0f), range);
            }
            if (s.link) {
                // Underline links and set drawing effect for blue color.
                layout->SetUnderline(TRUE, range);
                if (linkBrush) {
                    layout->SetDrawingEffect(linkBrush, range);
                }
            }
            if (s.strike) {
                layout->SetStrikethrough(TRUE, range);
            }
        }

        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);

        // Draw inline images (they take up space, not inline text).
        bool drewImages = false;
        for (const auto& ib : n.children) {
            if (ib.kind == InlineKind::Image && !ib.url.empty()) {
                // Convert URL from UTF-8 to UTF-32 for ImageHelper.
                std::u32string url32;
                for (unsigned char c : ib.url) {
                    if (c < 0x80) url32.push_back(static_cast<char32_t>(c));
                    else {
                        // Simple UTF-8 decode for URL.
                        // URLs are typically ASCII, so this is rare.
                        url32.push_back(static_cast<char32_t>(c));
                    }
                }
                ID2D1Bitmap* bmp = ImageHelper::LoadBitmapFromFile(rt, url32, drawW);
                if (bmp) {
                    D2D1_SIZE_F bmpSize = bmp->GetSize();
                    float drawW2 = drawW;
                    float drawH2 = drawW2 * (bmpSize.height / bmpSize.width);
                    if (drawH2 > 400.0f) { drawH2 = 400.0f; drawW2 = drawH2 * (bmpSize.width / bmpSize.height); }
                    D2D1_RECT_F dest = D2D1::RectF(drawX, curY, drawX + drawW2, curY + drawH2);
                    D2D1_RECT_F src = D2D1::RectF(0, 0, bmpSize.width, bmpSize.height);
                    rt->DrawBitmap(bmp, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
                    bmp->Release();
                    curY += drawH2 + kBlockGap;
                    drewImages = true;
                }
            }
        }
        if (drewImages) continue;

        // Draw inline code background (light gray) before text.
        ID2D1SolidColorBrush* codeBg = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0xF0F0F0), &codeBg);
        if (codeBg) {
            for (const auto& s : spans) {
                if (s.code) {
                    DWRITE_TEXT_METRICS tm = {};
                    layout->GetMetrics(&tm);
                    UINT32 hitCount = 0;
                    DWRITE_HIT_TEST_METRICS htm[64];
                    layout->HitTestTextRange(s.start, s.length,
                        textX, curY, htm, 64, &hitCount);
                    for (UINT32 h = 0; h < hitCount; ++h) {
                        D2D1_RECT_F r = D2D1::RectF(
                            htm[h].left - 2.0f, htm[h].top,
                            htm[h].left + htm[h].width + 2.0f,
                            htm[h].top + htm[h].height);
                        rt->FillRectangle(r, codeBg);
                    }
                }
            }
            codeBg->Release();
        }

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
        if (linkBrush) {
            ColorTextRenderer ctr(rt, black, linkBrush);
            layout->Draw(nullptr, &ctr, origin.x, origin.y);
        } else {
            rt->DrawTextLayout(origin, layout, black, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        // Draw quote border on the left.
        if (n.block == BlockKind::BlockQuote && quoteBorder) {
            D2D1_POINT_2F p1 = D2D1::Point2F(kPadX + 4.0f, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(kPadX + 4.0f, curY + metrics.height);
            rt->DrawLine(p1, p2, quoteBorder, 3.0f);
        }

        // Draw HR line under H1 headings.
        if (n.block == BlockKind::Heading && n.level <= 2) {
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
        prevBlock = n.block;
        prevDepth = n.depth;
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    if (black) black->Release();
    if (linkBrush) linkBrush->Release();
    if (quoteBorder) quoteBorder->Release();
    return curY;
}
