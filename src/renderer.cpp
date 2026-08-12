#include "renderer.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "colortext.h"
#include "imagehelper.h"

static const float kPtToDip = 96.0f / 72.0f;

Renderer::Renderer() {}
Renderer::~Renderer() { Release(); }

LayoutMetrics Renderer::ComputeMetrics() const {
    return ScaleMetrics(BaseMetrics(), zoom_);
}

float Renderer::GapForTransition(BlockKind prev, BlockKind cur,
                                  BlockKind next, int curDepth,
                                  int prevDepth, const LayoutMetrics& m) {
    // Heading spacing: large gap before, small gap after.
    if (cur == BlockKind::Heading) {
        if (prev == BlockKind::Heading) return m.headingGapAfter;
        // Gap before a heading (between previous block and this heading).
        return m.headingGapBefore;
    }
    if (prev == BlockKind::Heading) return m.headingGapAfter;

    // List spacing: tight between items, generous around the list.
    if (cur == BlockKind::List && prev == BlockKind::List) {
        return m.listItemGap;
    }
    if (cur == BlockKind::List && prev != BlockKind::List) {
        return m.listGap;
    }
    if (prev == BlockKind::List && cur != BlockKind::List) {
        return m.listGap;
    }

    // Code block spacing.
    if (cur == BlockKind::CodeBlock || prev == BlockKind::CodeBlock) {
        return m.codeBlockGap;
    }

    // Default paragraph gap.
    return m.paraGap;
}

bool Renderer::Init(IDWriteFactory* dw) {
    if (!dw) return false;

    LayoutMetrics base = BaseMetrics();

    // Body format: Segoe UI, 15pt.
    HRESULT hr = dw->CreateTextFormat(
        base.bodyFont, nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        base.bodyFontSize * kPtToDip * zoom_,
        L"", &body_fmt_);
    if (FAILED(hr) || !body_fmt_) return false;
    body_fmt_->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_PROPORTIONAL,
        base.bodyLineHeight, 1.24f);

    // Code format: Cascadia Mono, 12.5pt (falls back to Consolas).
    hr = dw->CreateTextFormat(
        base.codeFont, nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        base.codeFontSize * kPtToDip * zoom_,
        L"", &code_fmt_);
    if (FAILED(hr) || !code_fmt_) return false;
    code_fmt_->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_PROPORTIONAL,
        base.codeLineHeight, 1.0f);

    // Heading formats: Segoe UI Semibold, scaled by level.
    for (int lvl = 1; lvl <= 6; ++lvl) {
        hr = dw->CreateTextFormat(
            base.bodyFont, nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            base.headingSizes[lvl] * kPtToDip * zoom_,
            L"", &heading_fmt_[lvl]);
        if (FAILED(hr) || !heading_fmt_[lvl]) return false;
        heading_fmt_[lvl]->SetLineSpacing(
            DWRITE_LINE_SPACING_METHOD_PROPORTIONAL,
            base.headingLineHeight, 1.0f);
    }

    return true;
}

void Renderer::Release() {
    auto rel = [](IDWriteTextFormat*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(body_fmt_);
    rel(code_fmt_);
    for (int i = 1; i <= 6; ++i) rel(heading_fmt_[i]);
}

void Renderer::SetZoom(float z) {
    zoom_ = z;
}

void Renderer::SetWrap(bool w) {
    wrapEnabled_ = w;
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

    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();

    std::u32string raw = n.raw;
    while (!raw.empty() && (raw.back() == 0x0A || raw.back() == 0x0D)) {
        raw.pop_back();
    }
    std::u16string text16 = ToUtf16(raw);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(pal.codeBg, &bgBrush);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(pal.codeBorder, &borderBrush);
    ID2D1SolidColorBrush* codeBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &codeBrush);

    float codeWidth = width - 2.0f * m.codePad;
    if (codeWidth <= 0) {
        outH = 0.0f;
        if (bgBrush) bgBrush->Release();
        if (borderBrush) borderBrush->Release();
        if (codeBrush) codeBrush->Release();
        return;
    }

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(text16.data()),
        static_cast<UINT32>(text16.size()),
        code_fmt_, codeWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) {
        if (bgBrush) bgBrush->Release();
        if (borderBrush) borderBrush->Release();
        if (codeBrush) codeBrush->Release();
        outH = 0.0f;
        return;
    }

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);

    float blockH = metrics.height + 2.0f * m.codePad;

    // Rounded background.
    D2D1_RECT_F bgRect = D2D1::RectF(x, y, x + width, y + blockH);
    if (m.codeRadius > 0.5f) {
        D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bgRect,
            m.codeRadius, m.codeRadius);
        rt->FillRoundedRectangle(rrect, bgBrush);
        rt->DrawRoundedRectangle(rrect, borderBrush, 1.0f);
    } else {
        rt->FillRectangle(bgRect, bgBrush);
        rt->DrawRectangle(bgRect, borderBrush, 1.0f);
    }

    // Draw text.
    D2D1_POINT_2F origin = D2D1::Point2F(x + m.codePad, y + m.codePad);
    rt->DrawTextLayout(origin, layout, codeBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    layout->Release();
    if (bgBrush) bgBrush->Release();
    if (borderBrush) borderBrush->Release();
    if (codeBrush) codeBrush->Release();
    outH = blockH;
}

void Renderer::DrawThematicBreak(ID2D1RenderTarget* rt, float x, float y,
                                  float width) {
    Palette pal = BasePalette();
    ID2D1SolidColorBrush* lineBrush = nullptr;
    rt->CreateSolidColorBrush(pal.rule, &lineBrush);
    if (!lineBrush) return;
    D2D1_POINT_2F p1 = D2D1::Point2F(x, y);
    D2D1_POINT_2F p2 = D2D1::Point2F(x + width, y);
    rt->DrawLine(p1, p2, lineBrush, 1.0f);
    lineBrush->Release();
}

float Renderer::MeasureTable(IDWriteFactory* dw, const Node& n,
                               float x, float width) {
    if (!dw || !body_fmt_ || n.rows.empty()) return 0.0f;

    LayoutMetrics m = ComputeMetrics();

    size_t cols = 0;
    for (const auto& row : n.rows) {
        cols = (row.cells.size() > cols) ? row.cells.size() : cols;
    }
    if (cols == 0) return 0.0f;

    float colW = width / static_cast<float>(cols);
    float totalH = 0.0f;

    for (const auto& row : n.rows) {
        float rowH = 0.0f;
        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colW - 2.0f * m.cellPadX, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                DWRITE_TEXT_METRICS tm = {};
                layout->GetMetrics(&tm);
                if (tm.height > rowH) rowH = tm.height;
                layout->Release();
            }
        }
        if (rowH == 0.0f) rowH = 20.0f;
        totalH += rowH + 2.0f * m.cellPadY;
    }
    return totalH;
}

void Renderer::DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                          const Node& n, float x, float y,
                          float width, float& outH) {
    if (!rt || !dw || !body_fmt_ || n.rows.empty()) { outH = 0.0f; return; }

    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();

    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);
    ID2D1SolidColorBrush* headerBg = nullptr;
    rt->CreateSolidColorBrush(pal.tableHeaderBg, &headerBg);
    ID2D1SolidColorBrush* altBg = nullptr;
    rt->CreateSolidColorBrush(pal.tableRowAlt, &altBg);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(pal.tableBorder, &borderBrush);

    size_t cols = 0;
    for (const auto& row : n.rows) {
        cols = (row.cells.size() > cols) ? row.cells.size() : cols;
    }
    if (cols == 0) {
        outH = 0.0f;
        if (textBrush) textBrush->Release();
        if (headerBg) headerBg->Release();
        if (altBg) altBg->Release();
        if (borderBrush) borderBrush->Release();
        return;
    }

    float colW = width / static_cast<float>(cols);
    float curY = y;

    for (size_t ri = 0; ri < n.rows.size(); ++ri) {
        const auto& row = n.rows[ri];
        float rowH = 0.0f;
        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colW - 2.0f * m.cellPadX, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                DWRITE_TEXT_METRICS tm = {};
                layout->GetMetrics(&tm);
                if (tm.height > rowH) rowH = tm.height;
                layout->Release();
            }
        }
        if (rowH == 0.0f) rowH = 20.0f;
        rowH += 2.0f * m.cellPadY;

        bool isHeaderRow = (!row.cells.empty() && row.cells[0].isHeader);
        if (isHeaderRow && headerBg) {
            D2D1_RECT_F bg = D2D1::RectF(x, curY, x + width, curY + rowH);
            rt->FillRectangle(bg, headerBg);
        } else if (!isHeaderRow && altBg && (ri % 2 == 0)) {
            // Zebra striping on even body rows.
            D2D1_RECT_F bg = D2D1::RectF(x, curY, x + width, curY + rowH);
            rt->FillRectangle(bg, altBg);
        }

        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            float cellX = x + static_cast<float>(c) * colW;
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
                body_fmt_, colW - 2.0f * m.cellPadX, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0,
                        static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                }
                D2D1_POINT_2F origin = D2D1::Point2F(
                    cellX + m.cellPadX, curY + m.cellPadY);
                rt->DrawTextLayout(origin, layout, textBrush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
                layout->Release();
            }
        }
        if (borderBrush) {
            D2D1_POINT_2F p1 = D2D1::Point2F(x + width, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(x + width, curY + rowH);
            rt->DrawLine(p1, p2, borderBrush, 1.0f);
        }
        if (borderBrush) {
            rt->DrawLine(
                D2D1::Point2F(x, curY), D2D1::Point2F(x + width, curY),
                borderBrush, 1.0f);
            rt->DrawLine(
                D2D1::Point2F(x, curY + rowH),
                D2D1::Point2F(x + width, curY + rowH),
                borderBrush, 1.0f);
        }
        curY += rowH;
    }

    if (textBrush) textBrush->Release();
    if (headerBg) headerBg->Release();
    if (altBg) altBg->Release();
    if (borderBrush) borderBrush->Release();
    outH = curY - y;
}

float Renderer::Measure(IDWriteFactory* dw, const Document& doc,
                           float widthDip, float topOffsetDip) {
    if (!dw) return 0.0f;
    LayoutMetrics m = ComputeMetrics();
    if (widthDip <= 0.0f) return m.padTop + topOffsetDip;

    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : 10000.0f;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;

    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;

    float curY = m.padTop + topOffsetDip;
    BlockKind prevBlock = BlockKind::Paragraph;
    int prevDepth = -1;

    for (const auto& n : doc.nodes) {
        float blockH = 0.0f;
        float drawX = originX;
        float drawW = contentWidth;

        if (n.block == BlockKind::BlockQuote) {
            float quoteIndent = m.quoteIndent * (n.depth + 1);
            drawX += quoteIndent;
            drawW -= quoteIndent;
        } else if (n.block == BlockKind::List) {
            float listIndent = m.listIndent * (n.depth + 1);
            drawX += listIndent;
            drawW -= listIndent;
        }

        if (n.block == BlockKind::CodeBlock) {
            std::u32string raw = n.raw;
            while (!raw.empty() &&
                   (raw.back() == 0x0A || raw.back() == 0x0D)) {
                raw.pop_back();
            }
            std::u16string text16 = ToUtf16(raw);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                code_fmt_, drawW - 2.0f * m.codePad, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                DWRITE_TEXT_METRICS tm = {};
                layout->GetMetrics(&tm);
                blockH = tm.height + 2.0f * m.codePad;
                layout->Release();
            }
        } else if (n.block == BlockKind::ThematicBreak) {
            blockH = 12.0f;
        } else if (n.block == BlockKind::Table) {
            blockH = MeasureTable(dw, n, drawX, drawW);
        } else {
            IDWriteTextFormat* fmt = nullptr;
            if (n.block == BlockKind::Heading) {
                int lvl = n.level;
                if (lvl < 1) lvl = 1;
                if (lvl > 6) lvl = 6;
                fmt = heading_fmt_[lvl];
            } else if (n.block == BlockKind::Paragraph ||
                       n.block == BlockKind::BlockQuote ||
                       n.block == BlockKind::List) {
                fmt = body_fmt_;
            }
            if (fmt) {
                std::u32string text32;
                float markerW = 0.0f;
                if (n.block == BlockKind::List) {
                    markerW = m.listIndent * 0.6f + m.markerGutter;
                    text32 += n.ordered ? U"1. " : U"\u2022  ";
                }
                for (const auto& ib : n.children) text32 += ib.text;
                if (text32.empty()) {
                    curY += GapForTransition(prevBlock, n.block,
                        BlockKind::Paragraph, n.depth, prevDepth, m);
                    prevBlock = n.block;
                    prevDepth = n.depth;
                    continue;
                }
                std::u16string text16 = ToUtf16(text32);
                IDWriteTextLayout* layout = nullptr;
                float layoutW = drawW - markerW;
                HRESULT hr = dw->CreateTextLayout(
                    reinterpret_cast<const WCHAR*>(text16.data()),
                    static_cast<UINT32>(text16.size()),
                    fmt, layoutW > 0 ? layoutW : drawW, 1.0e9f, &layout);
                if (SUCCEEDED(hr) && layout) {
                    DWRITE_TEXT_METRICS tm = {};
                    layout->GetMetrics(&tm);
                    blockH = tm.height;
                    layout->Release();
                }
                if (n.block == BlockKind::Heading && n.level <= 2) {
                    blockH += m.ruleGapAbove + m.ruleGapBelow;
                }
            }
        }

        float gap = (prevBlock == BlockKind::Paragraph &&
                     n.block == BlockKind::Paragraph)
            ? 0 : GapForTransition(prevBlock, n.block,
                                   BlockKind::Paragraph,
                                   n.depth, prevDepth, m);
        // First block: no gap before it.
        bool isFirst = (prevBlock == BlockKind::Paragraph &&
                        prevDepth == -1 && curY == m.padTop + topOffsetDip);
        if (isFirst) gap = 0;

        curY += gap;
        curY += blockH;
        prevBlock = n.block;
        prevDepth = n.depth;
    }
    return curY;
}

float Renderer::Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Document& doc, float widthDip, float scrollY,
                       float topOffsetDip,
                       const Selection* sel) {
    if (!rt || !dw) return 0.0f;
    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();
    if (widthDip <= 0.0f) return m.padTop;

    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : 10000.0f;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;

    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;

    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);
    if (!textBrush) return m.padTop;

    ID2D1SolidColorBrush* headingBrush = nullptr;
    rt->CreateSolidColorBrush(pal.heading, &headingBrush);

    ID2D1SolidColorBrush* mutedBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textMuted, &mutedBrush);

    ID2D1SolidColorBrush* linkBrush = nullptr;
    rt->CreateSolidColorBrush(pal.link, &linkBrush);

    ID2D1SolidColorBrush* quoteBorder = nullptr;
    rt->CreateSolidColorBrush(pal.quoteBar, &quoteBorder);

    ID2D1SolidColorBrush* selBrush = nullptr;
    if (sel && !sel->Empty()) {
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
    }

    // Clip to content area (below the ribbon).
    D2D1_SIZE_F clipSize = rt->GetSize();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PushAxisAlignedClip(
        D2D1::RectF(0.0f, topOffsetDip, clipSize.width, clipSize.height),
        (D2D1_ANTIALIAS_MODE)0);

    rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -scrollY));

    // Clear the layout cache before rebuilding.
    if (cache_) cache_->Clear();

    float curY = m.padTop + topOffsetDip;
    int listCounter = 0;
    BlockKind prevBlock = BlockKind::Paragraph;
    int prevDepth = -1;

    for (size_t nodeIdx = 0; nodeIdx < doc.nodes.size(); ++nodeIdx) {
        const auto& n = doc.nodes[nodeIdx];
        float drawX = originX;
        float drawW = contentWidth;

        if (n.block == BlockKind::BlockQuote) {
            float quoteIndent = m.quoteIndent * (n.depth + 1);
            drawX += quoteIndent;
            drawW -= quoteIndent;
        } else if (n.block == BlockKind::List) {
            float listIndent = m.listIndent * (n.depth + 1);
            drawX += listIndent;
            drawW -= listIndent;
        }

        float gap = 0.0f;
        bool isFirst = (prevBlock == BlockKind::Paragraph &&
                        prevDepth == -1 &&
                        curY == m.padTop + topOffsetDip);
        if (!isFirst) {
            gap = GapForTransition(prevBlock, n.block,
                                   BlockKind::Paragraph,
                                   n.depth, prevDepth, m);
        }
        curY += gap;

        if (n.block == BlockKind::CodeBlock) {
            float blockH = 0.0f;
            DrawCodeBlock(rt, dw, n, drawX, curY, drawW, blockH);
            curY += blockH + 0;
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::Table) {
            float blockH = 0.0f;
            DrawTable(rt, dw, n, drawX, curY, drawW, blockH);
            curY += blockH;
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::ThematicBreak) {
            DrawThematicBreak(rt, drawX, curY + 6.0f, drawW);
            curY += 12.0f;
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        IDWriteTextFormat* fmt = nullptr;
        ID2D1SolidColorBrush* blockBrush = textBrush;
        if (n.block == BlockKind::Heading) {
            int lvl = n.level;
            if (lvl < 1) lvl = 1;
            if (lvl > 6) lvl = 6;
            fmt = heading_fmt_[lvl];
            blockBrush = headingBrush ? headingBrush : textBrush;
        } else if (n.block == BlockKind::Paragraph ||
                   n.block == BlockKind::BlockQuote ||
                   n.block == BlockKind::List) {
            fmt = body_fmt_;
            if (n.block == BlockKind::BlockQuote && mutedBrush)
                blockBrush = mutedBrush;
        }

        if (!fmt) {
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        std::u32string text32;
        float markerW = 0.0f;
        std::u16string marker16;
        if (n.block == BlockKind::List) {
            markerW = m.listIndent * 0.6f + m.markerGutter;
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
            if (ib.kind == InlineKind::Image) continue;
            std::u16string part16 = ToUtf16(ib.text);
            UINT32 start = static_cast<UINT32>(text16.size());
            text16 += part16;
            UINT32 length = static_cast<UINT32>(part16.size());
            if (length > 0) {
                spans.push_back({start, length, ib.em, ib.strong,
                                 ib.code, ib.kind == InlineKind::Link,
                                 ib.strike});
            }
        }

        if (text16.empty() && marker16.empty()) {
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        float textX = drawX + markerW;
        float textW = drawW - markerW;

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(text16.data()),
            static_cast<UINT32>(text16.size()),
            fmt, textW > 0 ? textW : drawW, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) {
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        LayoutMetrics base = BaseMetrics();
        for (const auto& s : spans) {
            DWRITE_TEXT_RANGE range = {s.start, s.length};
            if (s.strong) {
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            }
            if (s.em) {
                layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            }
            if (s.code) {
                layout->SetFontFamilyName(base.codeFont, range);
                layout->SetFontSize(
                    base.codeFontSize * kPtToDip * zoom_, range);
            }
            if (s.link) {
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

        bool drewImages = false;
        for (const auto& ib : n.children) {
            if (ib.kind == InlineKind::Image && !ib.url.empty()) {
                ID2D1Bitmap* bmp = ImageHelper::LoadBitmapFromUrl(
                    rt, ib.url, drawW);
                if (bmp) {
                    D2D1_SIZE_F bmpSize = bmp->GetSize();
                    float drawW2 = drawW;
                    float drawH2 = drawW2 *
                        (bmpSize.height / bmpSize.width);
                    if (drawH2 > 400.0f) {
                        drawH2 = 400.0f;
                        drawW2 = drawH2 *
                            (bmpSize.width / bmpSize.height);
                    }
                    D2D1_RECT_F dest = D2D1::RectF(
                        drawX, curY, drawX + drawW2, curY + drawH2);
                    D2D1_RECT_F src = D2D1::RectF(
                        0, 0, bmpSize.width, bmpSize.height);
                    rt->DrawBitmap(bmp, dest, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
                    bmp->Release();
                    curY += drawH2 + m.paraGap;
                    drewImages = true;
                }
            }
        }
        if (drewImages) {
            layout->Release();
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        ID2D1SolidColorBrush* codeBg = nullptr;
        rt->CreateSolidColorBrush(pal.inlineCodeBg, &codeBg);
        if (codeBg) {
            for (const auto& s : spans) {
                if (s.code) {
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

        if (!marker16.empty()) {
            IDWriteTextLayout* markerLayout = nullptr;
            hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(marker16.data()),
                static_cast<UINT32>(marker16.size()),
                fmt, markerW, 1.0e9f, &markerLayout);
            if (SUCCEEDED(hr) && markerLayout) {
                D2D1_POINT_2F mOrigin = D2D1::Point2F(drawX, curY);
                rt->DrawTextLayout(mOrigin, markerLayout, blockBrush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
                markerLayout->Release();
            }
        }

        D2D1_POINT_2F origin = D2D1::Point2F(textX, curY);
        if (linkBrush) {
            ColorTextRenderer ctr(rt, blockBrush, linkBrush);
            layout->Draw(nullptr, &ctr, origin.x, origin.y);
        } else {
            rt->DrawTextLayout(origin, layout, blockBrush,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if (n.block == BlockKind::BlockQuote && quoteBorder) {
            float barX = drawX - m.quoteIndent + 4.0f;
            D2D1_POINT_2F p1 = D2D1::Point2F(barX, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(barX, curY + metrics.height);
            rt->DrawLine(p1, p2, quoteBorder, m.quoteBarWidth);
        }

        if (n.block == BlockKind::Heading && n.level <= 2) {
            ID2D1SolidColorBrush* hrBrush = nullptr;
            rt->CreateSolidColorBrush(pal.rule, &hrBrush);
            if (hrBrush) {
                float ruleY = curY + metrics.height + m.ruleGapAbove;
                D2D1_POINT_2F p1 = D2D1::Point2F(drawX, ruleY);
                D2D1_POINT_2F p2 = D2D1::Point2F(
                    drawX + drawW, ruleY);
                rt->DrawLine(p1, p2, hrBrush, 1.0f);
                hrBrush->Release();
            }
            curY += m.ruleGapAbove + m.ruleGapBelow;
        }

        if (cache_) {
            BlockLayout bl;
            bl.layout = layout;
            bl.x = textX;
            bl.y = curY;
            bl.width = textW > 0 ? textW : drawW;
            bl.height = metrics.height;
            bl.srcOffset = n.srcOffset;
            bl.srcLength = n.srcLength;
            bl.textStartOffset = n.contentOffset;
            bl.nodeIndex = nodeIdx;
            cache_->Add(bl);
        } else {
            layout->Release();
        }
        curY += metrics.height;
        prevBlock = n.block;
        prevDepth = n.depth;
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PopAxisAlignedClip();

    if (textBrush) textBrush->Release();
    if (headingBrush) headingBrush->Release();
    if (mutedBrush) mutedBrush->Release();
    if (linkBrush) linkBrush->Release();
    if (quoteBorder) quoteBorder->Release();
    if (selBrush) selBrush->Release();
    return curY;
}
