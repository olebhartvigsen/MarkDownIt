#include "renderer.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "colortext.h"
#include "imagehelper.h"
#include "mermaid/model.h"
#include "mermaid/layout_internal.h"
#include "mermaid/layout.h"
#include "mermaid/layout_cache.h"
#include <cmath>

static const float kPtToDip = 96.0f / 72.0f;

Renderer::Renderer() {}
Renderer::~Renderer() { Release(); }

LayoutMetrics Renderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(BaseMetrics(), zoom_);
    // Override the content width cap based on the user's width setting.
    // Mode 3 (Full) means no cap, so we use a very large value.
    if (contentWidthMode_ == 0) {
        m.maxContentWidth = 800.0f * zoom_;       // Standard (default)
    } else if (contentWidthMode_ == 1) {
        m.maxContentWidth = 960.0f * zoom_;
    } else if (contentWidthMode_ == 2) {
        m.maxContentWidth = 1600.0f * zoom_;
    } else {
        m.maxContentWidth = 100000.0f;            // Full window width
    }
    return m;
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

    // Sequence autonumber digits: mermaid renders 12px sans-serif.
    hr = dw->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f * (4.0f / 3.0f) * zoom_,
        L"", &num_fmt_);
    if (FAILED(hr)) num_fmt_ = nullptr;

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
    rel(num_fmt_);
    for (int i = 1; i <= 6; ++i) rel(heading_fmt_[i]);
}

void Renderer::SetZoom(float z) {
    zoom_ = z;
}

void Renderer::SetWrap(bool w) {
    wrapEnabled_ = w;
}

void Renderer::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) mode = 0;
    contentWidthMode_ = mode;
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
                              float width, float& outH,
                              const Selection* sel) {
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

    // Draw selection highlight behind the text.
    if (sel && !sel->Empty() && !raw.empty() && !text16.empty()) {
        ID2D1SolidColorBrush* selBrush = nullptr;
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
        if (selBrush) {
            uint32_t selStart = sel->Start();
            uint32_t selEnd = selStart + sel->Length();
            uint32_t blockStart = n.srcOffset;
            uint32_t blockEnd = blockStart + n.srcLength;
            if (selStart < blockEnd && selEnd > blockStart) {
                // Map selection offsets to the raw text (contentOffset based).
                uint32_t textStart = n.contentOffset;
                uint32_t textEnd = textStart + n.contentLength;
                uint32_t localStart = (selStart > textStart) ?
                    (selStart - textStart) : 0;
                uint32_t localEnd = (selEnd < textEnd) ?
                    (selEnd - textStart) :
                    (textEnd > textStart ? textEnd - textStart : 0);
                // Convert UTF-8 local offsets to UTF-16 indices.
                // Walk the raw text to build the mapping inline.
                std::u32string::const_iterator it = raw.begin();
                UINT32 u16Start = 0, u16End = 0;
                uint32_t byteIdx = 0;
                bool pastStart = false, pastEnd = false;
                for (char32_t cp : raw) {
                    int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                  (cp <= 0xFFFF) ? 3 : 4;
                    int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
                    if (!pastStart && byteIdx >= localStart) {
                        u16Start = u16End;
                        pastStart = true;
                    }
                    if (!pastEnd && byteIdx >= localEnd) {
                        pastEnd = true;
                        break;
                    }
                    u16End += utf16Len;
                    byteIdx += utf8Len;
                }
                if (!pastStart) u16Start = u16End;  // past end
                if (!pastEnd) u16End = static_cast<UINT32>(text16.size());
                if (u16End > u16Start) {
                    UINT32 hitCount = 0;
                    DWRITE_HIT_TEST_METRICS htm[64];
                    D2D1_POINT_2F origin2 = D2D1::Point2F(
                        x + m.codePad, y + m.codePad);
                    HRESULT hrHit = layout->HitTestTextRange(
                        u16Start, u16End - u16Start,
                        origin2.x, origin2.y, htm, 64, &hitCount);
                    if (SUCCEEDED(hrHit)) {
                        for (UINT32 h = 0; h < hitCount && h < 64; ++h) {
                            D2D1_RECT_F r = D2D1::RectF(
                                htm[h].left, htm[h].top,
                                htm[h].left + htm[h].width,
                                htm[h].top + htm[h].height);
                            rt->FillRectangle(r, selBrush);
                        }
                    }
                }
            }
            selBrush->Release();
        }
    }

    // Draw text.
    D2D1_POINT_2F origin = D2D1::Point2F(x + m.codePad, y + m.codePad);
    rt->DrawTextLayout(origin, layout, codeBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    if (cache_) {
        // Build u16ToSrc mapping for the code block.
        std::vector<uint32_t> cu16ToSrc;
        uint32_t srcByte = n.contentOffset;
        for (char32_t cp : raw) {
            int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                          (cp <= 0xFFFF) ? 3 : 4;
            int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
            for (int u = 0; u < utf16Len; u++)
                cu16ToSrc.push_back(srcByte);
            srcByte += utf8Len;
        }
        BlockLayout bl;
        bl.layout = layout;
        bl.x = x + m.codePad;
        bl.y = y + m.codePad;
        bl.width = codeWidth;
        bl.height = metrics.height;
        bl.srcOffset = n.srcOffset;
        bl.srcLength = n.srcLength;
        bl.textStartOffset = n.contentOffset;
        bl.nodeIndex = 0;
        bl.u16ToSrc = std::move(cu16ToSrc);
        // Store the font em height for correct caret sizing.
        if (code_fmt_) {
            bl.fontHeight = code_fmt_->GetFontSize();
        }
        cache_->Add(bl);
    } else {
        layout->Release();
    }
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
                // Apply inline formatting for accurate height (code
                // spans use a wider monospace font that wraps more).
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0,
                        static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                }
                for (const auto& sp : row.cells[c].inlineSpans) {
                    DWRITE_TEXT_RANGE r = {sp.u16Start,
                        sp.u16End - sp.u16Start};
                    if (sp.bold)
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    if (sp.italic)
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    if (sp.code)
                        layout->SetFontFamilyName(L"Consolas", r);
                    if (sp.strike)
                        layout->SetStrikethrough(true, r);
                }
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
                          float width, float& outH,
                          const Selection* sel) {
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
    ID2D1SolidColorBrush* selBrush = nullptr;
    if (sel && !sel->Empty())
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);

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
        if (selBrush) selBrush->Release();
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
                // Apply the same inline formatting as the draw pass,
                // because code spans use a wider monospace font that
                // may wrap differently and need more vertical space.
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0,
                        static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                }
                for (const auto& sp : row.cells[c].inlineSpans) {
                    DWRITE_TEXT_RANGE r = {sp.u16Start,
                        sp.u16End - sp.u16Start};
                    if (sp.bold)
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    if (sp.italic)
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    if (sp.code)
                        layout->SetFontFamilyName(L"Consolas", r);
                    if (sp.strike)
                        layout->SetStrikethrough(true, r);
                }
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
                // Apply inline formatting spans (bold, italic, code, strike).
                for (const auto& sp : row.cells[c].inlineSpans) {
                    DWRITE_TEXT_RANGE r = {sp.u16Start,
                        sp.u16End - sp.u16Start};
                    if (sp.bold)
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    if (sp.italic)
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    if (sp.code) {
                        // Use a monospace font for inline code spans.
                        layout->SetFontFamilyName(L"Consolas", r);
                    }
                    if (sp.strike)
                        layout->SetStrikethrough(true, r);
                }
                D2D1_POINT_2F cellOrigin = D2D1::Point2F(
                    cellX + m.cellPadX, curY + m.cellPadY);
                // Draw selection highlight for this cell.
                if (selBrush && sel && !sel->Empty() &&
                    !row.cells[c].text.empty() && !text16.empty()) {
                    const auto& cell = row.cells[c];
                    uint32_t selStart = sel->Start();
                    uint32_t selEnd = selStart + sel->Length();
                    // Use cell.srcOffset and cell text length.
                    uint32_t cellStart = cell.srcOffset;
                    // Compute cell text length from the UTF-32 text.
                    uint32_t cellTextLen = 0;
                    for (char32_t cp : cell.text) {
                        cellTextLen += (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                       (cp <= 0xFFFF) ? 3 : 4;
                    }
                    uint32_t cellEnd = cellStart + cellTextLen;
                    if (selStart < cellEnd && selEnd > cellStart) {
                        uint32_t localStart = (selStart > cellStart) ?
                            (selStart - cellStart) : 0;
                        uint32_t localEnd = (selEnd < cellEnd) ?
                            (selEnd - cellStart) : cellTextLen;
                        // Convert UTF-8 offsets to UTF-16 indices.
                        UINT32 u16Start = 0, u16End = 0;
                        uint32_t byteIdx = 0;
                        bool pastStart = false, pastEnd = false;
                        for (char32_t cp : cell.text) {
                            int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                          (cp <= 0xFFFF) ? 3 : 4;
                            if (!pastStart && byteIdx >= localStart) {
                                u16Start = u16End;
                                pastStart = true;
                            }
                            if (!pastEnd && byteIdx >= localEnd) {
                                pastEnd = true;
                                break;
                            }
                            u16End += (cp <= 0xFFFF) ? 1 : 2;
                            byteIdx += utf8Len;
                        }
                        if (!pastStart) u16Start = u16End;
                        if (!pastEnd) u16End = static_cast<UINT32>(text16.size());
                        if (u16End > u16Start) {
                            UINT32 hitCount = 0;
                            DWRITE_HIT_TEST_METRICS htm[64];
                            HRESULT hrHit = layout->HitTestTextRange(
                                u16Start, u16End - u16Start,
                                cellOrigin.x, cellOrigin.y,
                                htm, 64, &hitCount);
                            if (SUCCEEDED(hrHit)) {
                                for (UINT32 h = 0; h < hitCount && h < 64; ++h) {
                                    D2D1_RECT_F r = D2D1::RectF(
                                        htm[h].left, htm[h].top,
                                        htm[h].left + htm[h].width,
                                        htm[h].top + htm[h].height);
                                    rt->FillRectangle(r, selBrush);
                                }
                            }
                        }
                    }
                }
                rt->DrawTextLayout(cellOrigin, layout, textBrush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
                if (cache_) {
                    // Add cell layout to cache for hit-testing (double-click,
                    // selection, caret placement in edit mode).
                    // Use the u16ToSrc mapping from the parser, which correctly
                    // handles gaps from md4c mark splits (e.g., ':' as a
                    // permissive URL autolink mark).
                    BlockLayout bl;
                    bl.layout = layout;
                    bl.x = cellOrigin.x;
                    bl.y = cellOrigin.y;
                    bl.width = colW - 2.0f * m.cellPadX;
                    bl.height = rowH - 2.0f * m.cellPadY;
                    bl.srcOffset = row.cells[c].srcOffset;
                    uint32_t cellTextLen = 0;
                    for (char32_t cp : row.cells[c].text) {
                        cellTextLen += (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                       (cp <= 0xFFFF) ? 3 : 4;
                    }
                    bl.srcLength = cellTextLen;
                    // Compute extended cell range: scan backward past
                    // opening markers and forward past closing markers
                    // so the caret works when placed between hidden
                    // marker characters (* ` ~).
                    bl.srcCellStart = bl.srcOffset;
                    bl.srcCellEnd = bl.srcOffset + cellTextLen;
                    if (srcText_) {
                        const auto& src = *srcText_;
                        // Scan backward past opening markers.
                        uint32_t st = bl.srcOffset;
                        while (st > 0 &&
                               (src[st - 1] == '*' || src[st - 1] == '`' ||
                                src[st - 1] == '~'))
                            st--;
                        bl.srcCellStart = st;
                        // Scan forward past closing markers.
                        uint32_t e = bl.srcOffset + cellTextLen;
                        while (e < src.size() &&
                               (src[e] == '*' || src[e] == '`' ||
                                src[e] == '~'))
                            e++;
                        bl.srcCellEnd = e;
                    }
                    bl.textStartOffset = row.cells[c].srcOffset;
                    bl.nodeIndex = 0;
                    bl.u16ToSrc = row.cells[c].u16ToSrc;  // copy parser's mapping
                    if (body_fmt_) {
                        bl.fontHeight = body_fmt_->GetFontSize();
                    }
                    cache_->Add(bl);
                } else {
                    layout->Release();
                }
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
    if (selBrush) selBrush->Release();
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
            // SVG blocks (and only those) measure via the SVG document.
            float svgH = 0.0f;
            if (n.lang == "svg") {
                svgH = MeasureSvgBlock(dw, n, drawX, drawW);
            }
            if (svgH > 0.0f) {
                blockH = svgH;
            } else {
                // All other code blocks (including mermaid source shown as
                // code) are measured as text. Without this, blockH stays 0
                // and totalH_ underestimates the document, breaking scroll.
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
            }
        } else if (n.block == BlockKind::ThematicBreak) {
            blockH = 12.0f;
        } else if (n.block == BlockKind::MermaidFlowchart) {
            blockH = MeasureMermaidBlock(n);
        } else if (n.block == BlockKind::MermaidPie) {
            blockH = mermaid::MeasurePieHeight(*n.mermaid_pie, zoom_);
        } else if (n.block == BlockKind::MermaidSequence) {
            blockH = mermaid::MeasureSequenceHeight(*n.mermaid_seq, zoom_);
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

        float gap = GapForTransition(prevBlock, n.block,
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
    // Bottom padding so the last block is not glued to the viewport
    // edge at maximum scroll. Double the top margin reads better at
    // the end of a document.
    return curY + m.padTop * 2.0f;
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

        if (n.block == BlockKind::CodeBlock &&
            n.lang == "svg") {
            float blockH = 0.0f;
            DrawSvgBlock(rt, dw, n, drawX, curY, drawW, blockH, sel);
            curY += blockH;
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::CodeBlock) {
            float blockH = 0.0f;
            DrawCodeBlock(rt, dw, n, drawX, curY, drawW, blockH, sel);
            curY += blockH + 0;
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::Table) {
            float blockH = 0.0f;
            DrawTable(rt, dw, n, drawX, curY, drawW, blockH, sel);
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

        if (n.block == BlockKind::MermaidFlowchart) {
            DrawMermaidBlock(rt, dw, n, drawX, curY);
            curY += MeasureMermaidBlock(n);
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::MermaidPie) {
            DrawMermaidPieBlock(rt, dw, n, drawX, curY);
            curY += mermaid::MeasurePieHeight(*n.mermaid_pie, zoom_);
            prevBlock = n.block;
            prevDepth = n.depth;
            continue;
        }

        if (n.block == BlockKind::MermaidSequence) {
            DrawMermaidSequenceBlock(rt, dw, n, drawX, curY);
            curY += mermaid::MeasureSequenceHeight(*n.mermaid_seq, zoom_);
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
        // Map each UTF-16 position to UTF-8 source byte offset.
        // Walk each inline block codepoint by codepoint to build this.
        std::vector<uint32_t> u16ToSrc;

        for (const auto& ib : n.children) {
            if (ib.kind == InlineKind::Image) continue;
            std::u16string part16 = ToUtf16(ib.text);
            UINT32 start = static_cast<UINT32>(text16.size());
            text16 += part16;
            UINT32 length = static_cast<UINT32>(part16.size());
            // Walk the UTF-32 codepoints and for each, compute its UTF-8
            // byte size and its UTF-16 code unit count, advancing both.
            uint32_t srcByte = ib.srcOffset;
            size_t u16Idx = start;
            for (char32_t cp : ib.text) {
                // UTF-8 byte length of this codepoint
                int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                              (cp <= 0xFFFF) ? 3 : 4;
                // UTF-16 code unit count
                int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
                for (int u = 0; u < utf16Len; u++) {
                    u16ToSrc.push_back(srcByte);
                }
                srcByte += utf8Len;
                u16Idx += utf16Len;
            }
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
                // Check if this is an SVG image.
                std::string urlLower = ib.url;
                for (auto& ch : urlLower)
                    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
                bool isSvg = (urlLower.size() > 4 &&
                    urlLower.substr(urlLower.size() - 4) == ".svg") ||
                    (urlLower.find("data:image/svg+xml") == 0);

                if (isSvg && d2d_ctx5_) {
                    // Load SVG text and render via SvgDoc.
                    std::string svgText = ImageHelper::LoadSvgText(ib.url);
                    if (!svgText.empty()) {
                        svg::SvgDoc svgDoc;
                        if (svgDoc.Load(d2d_ctx5_, svgText)) {
                            float drawW2 = drawW;
                            float drawH2 = (svgDoc.Width() > 0)
                                ? drawW2 * (svgDoc.Height() / svgDoc.Width())
                                : 100.0f;
                            if (drawH2 > 400.0f) {
                                drawH2 = 400.0f;
                                drawW2 = (svgDoc.Height() > 0)
                                    ? drawH2 * (svgDoc.Width() / svgDoc.Height())
                                    : drawW2;
                            }
                            svgDoc.Draw(d2d_ctx5_, dw, drawX, curY,
                                        drawW2, drawH2);
                            curY += drawH2 + m.paraGap;
                            drewImages = true;
                        }
                    }
                } else {
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
        }
        if (drewImages) {
            // Images advance curY; draw the paragraph text below them
            // instead of dropping it (the old code released the layout
            // and skipped the text entirely).
            rt->DrawTextLayout(D2D1::Point2F(textX, curY), layout, textBrush);
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
                    HRESULT hrHT = layout->HitTestTextRange(s.start, s.length,
                        textX, curY, htm, 64, &hitCount);
                    if (SUCCEEDED(hrHT)) {
                        for (UINT32 h = 0; h < hitCount && h < 64; ++h) {
                            D2D1_RECT_F r = D2D1::RectF(
                                htm[h].left - 2.0f, htm[h].top,
                                htm[h].left + htm[h].width + 2.0f,
                                htm[h].top + htm[h].height);
                            rt->FillRectangle(r, codeBg);
                        }
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

        // Draw selection highlight behind the text.
        if (selBrush && sel && !sel->Empty() && !u16ToSrc.empty()) {
            uint32_t selStart = sel->Start();
            uint32_t selEnd = selStart + sel->Length();
            uint32_t blockStart = n.srcOffset;
            uint32_t blockEnd = blockStart + n.srcLength;
            if (selStart < blockEnd && selEnd > blockStart) {
                // Find UTF-16 positions for the selection boundaries
                // by binary search on u16ToSrc.
                auto findU16 = [&](uint32_t srcOff) -> UINT32 {
                    // Find first u16 index where u16ToSrc[idx] >= srcOff.
                    // If not found, return text16.size().
                    if (srcOff <= u16ToSrc[0]) return 0;
                    size_t lo = 0, hi = u16ToSrc.size();
                    while (lo < hi) {
                        size_t mid = (lo + hi) / 2;
                        if (u16ToSrc[mid] < srcOff) lo = mid + 1;
                        else hi = mid;
                    }
                    return (lo < u16ToSrc.size()) ?
                        static_cast<UINT32>(lo) :
                        static_cast<UINT32>(text16.size());
                };
                UINT32 u16Start = findU16(selStart);
                UINT32 u16End = (selEnd >= blockEnd) ?
                    static_cast<UINT32>(text16.size()) :
                    findU16(selEnd);
                if (u16End > u16Start) {
                    UINT32 hitCount = 0;
                    DWRITE_HIT_TEST_METRICS htm[64];
                    HRESULT hrHit = layout->HitTestTextRange(
                        u16Start, u16End - u16Start,
                        textX, curY, htm, 64, &hitCount);
                    if (SUCCEEDED(hrHit)) {
                        for (UINT32 h = 0; h < hitCount && h < 64; ++h) {
                            D2D1_RECT_F r = D2D1::RectF(
                                htm[h].left, htm[h].top,
                                htm[h].left + htm[h].width,
                                htm[h].top + htm[h].height);
                            rt->FillRectangle(r, selBrush);
                        }
                    }
                }
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
            bl.u16ToSrc = std::move(u16ToSrc);
            // Store the font em height for correct caret sizing.
            if (fmt) {
                bl.fontHeight = fmt->GetFontSize();
            }
            cache_->Add(bl);
        } else {
            layout->Release();
        }

        // Advance curY past the text AND the H1/H2 underline gap.
        curY += metrics.height;
        if (n.block == BlockKind::Heading && n.level <= 2) {
            curY += m.ruleGapAbove + m.ruleGapBelow;
        }
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

// --- Source view: render raw markdown as monospace text with word wrap ---

static std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char16_t>(c);
            i += 1;
        } else if (c < 0xC0) {
            i += 1;  // skip continuation byte
        } else if (c < 0xE0) {
            if (i + 1 < s.size()) {
                char16_t ch = ((c & 0x1F) << 6) |
                    (static_cast<unsigned char>(s[i+1]) & 0x3F);
                out += ch;
                i += 2;
            } else { i += 1; }
        } else if (c < 0xF0) {
            if (i + 2 < s.size()) {
                char16_t ch = ((c & 0x0F) << 12) |
                    ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(s[i+2]) & 0x3F);
                out += ch;
                i += 3;
            } else { i += 1; }
        } else {
            if (i + 3 < s.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                    ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 12) |
                    ((static_cast<unsigned char>(s[i+2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(s[i+3]) & 0x3F);
                cp -= 0x10000;
                out += static_cast<char16_t>(0xD800 + (cp >> 10));
                out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
                i += 4;
            } else { i += 1; }
        }
    }
    return out;
}

float Renderer::MeasureSourceView(IDWriteFactory* dw, const std::string& src,
                                   float widthDip, float topOffsetDip) {
    if (!dw || !code_fmt_) return 0.0f;

    LayoutMetrics m = ComputeMetrics();
    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : 10000.0f;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;
    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;
    float textW = contentWidth - 2.0f * m.cellPadX;

    std::u16string text16 = Utf8ToUtf16(src);
    if (text16.empty()) return topOffsetDip + 20.0f;

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(text16.data()),
        static_cast<UINT32>(text16.size()),
        code_fmt_, textW > 0 ? textW : contentWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) return topOffsetDip + 20.0f;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);
    float totalH = m.padTop + topOffsetDip + tm.height + m.padTop;
    layout->Release();
    return totalH;
}

float Renderer::RenderSourceView(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                  const std::string& src, float widthDip,
                                  float scrollY, float topOffsetDip,
                                  const Selection* sel) {
    if (!rt || !dw || !code_fmt_) return topOffsetDip;

    // Own the cache lifecycle like Render() does: clear stale blocks so
    // repeated calls never accumulate layout-owned entries.
    if (cache_) cache_->Clear();

    Palette pal = BasePalette();
    LayoutMetrics m = ComputeMetrics();
    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : 10000.0f;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;
    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;
    float textW = contentWidth - 2.0f * m.cellPadX;
    float textX = originX + m.cellPadX;
    float textY = m.padTop + topOffsetDip;

    std::u16string text16 = Utf8ToUtf16(src);
    if (text16.empty()) return topOffsetDip + 20.0f;

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(text16.data()),
        static_cast<UINT32>(text16.size()),
        code_fmt_, textW > 0 ? textW : contentWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) return m.padTop + topOffsetDip + 20.0f;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);

    // Clip to the content area (document coordinates; transform handles scrollY).
    D2D1_RECT_F clipRect = D2D1::RectF(
        originX, textY, originX + contentWidth, textY + tm.height + 50.0f);
    rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_ALIASED);

    // Translate for scrolling.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -scrollY));

    // Draw text.
    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);
    ID2D1SolidColorBrush* selBrush = nullptr;
    rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);

    // Draw selection highlight behind the text.
    // Build u16ToSrc mapping first (needed for binary search).
    std::vector<uint32_t> u16ToSrcMap;
    {
        uint32_t bi = 0;
        for (size_t si = 0; si < src.size(); ) {
            unsigned char c = static_cast<unsigned char>(src[si]);
            int utf8Len = (c < 0x80) ? 1 : (c < 0xC0) ? 1 :
                          (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            int utf16Len = (c < 0x80) ? 1 : (c < 0xE0) ? 1 :
                           (c < 0xF0) ? 1 : 2;
            for (int u = 0; u < utf16Len; ++u) {
                u16ToSrcMap.push_back(bi);
            }
            bi += utf8Len;
            si += utf8Len;
        }
    }
    if (selBrush && sel && !sel->Empty() && !u16ToSrcMap.empty()) {
        uint32_t selStart = sel->Start();
        uint32_t selEnd = selStart + sel->Length();
        // Binary search: find first u16 index where u16ToSrc[idx] >= srcOff.
        auto findU16 = [&](uint32_t srcOff) -> UINT32 {
            if (srcOff <= u16ToSrcMap[0]) return 0;
            size_t lo = 0, hi = u16ToSrcMap.size();
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (u16ToSrcMap[mid] < srcOff) lo = mid + 1;
                else hi = mid;
            }
            return (lo < u16ToSrcMap.size()) ?
                static_cast<UINT32>(lo) :
                static_cast<UINT32>(text16.size());
        };
        UINT32 u16Start = findU16(selStart);
        UINT32 u16End = (selEnd >= static_cast<uint32_t>(src.size())) ?
            static_cast<UINT32>(text16.size()) : findU16(selEnd);
        if (u16End > u16Start) {
            UINT32 hitCount = 0;
            DWRITE_HIT_TEST_METRICS htm[64];
            HRESULT hrHit = layout->HitTestTextRange(
                u16Start, u16End - u16Start,
                textX, textY,
                htm, 64, &hitCount);
            if (SUCCEEDED(hrHit)) {
                for (UINT32 h = 0; h < hitCount && h < 64; ++h) {
                    D2D1_RECT_F r = D2D1::RectF(
                        htm[h].left, htm[h].top,
                        htm[h].left + htm[h].width,
                        htm[h].top + htm[h].height);
                    rt->FillRectangle(r, selBrush);
                }
            }
        }
    }

    // Draw the text in document coordinates (transform handles scrollY).
    rt->DrawTextLayout(D2D1::Point2F(textX, textY),
                       layout, textBrush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Add block layout to cache for hit-testing and caret placement.
    if (cache_) {
        BlockLayout bl;
        bl.layout = layout;
        bl.x = textX;
        bl.y = textY;
        bl.width = textW > 0 ? textW : contentWidth;
        bl.height = tm.height;
        // Extend the block's y range to cover the full viewport so
        // that margin clicks at any visible y find this block even
        // when the text is shorter than the viewport.
        D2D1_SIZE_F rtSize = rt->GetSize();
        float viewportH = rtSize.height;
        if (bl.height < viewportH) bl.height = viewportH + m.padTop;
        bl.srcOffset = 0;
        bl.srcLength = static_cast<uint32_t>(src.size());
        bl.textStartOffset = 0;
        bl.nodeIndex = 0;
        bl.fontHeight = code_fmt_->GetFontSize();
        // Build u16ToSrc mapping: each UTF-16 code unit maps to its
        // source byte offset.
        uint32_t bi = 0;
        for (size_t si = 0; si < src.size(); ) {
            unsigned char c = static_cast<unsigned char>(src[si]);
            int utf8Len = (c < 0x80) ? 1 : (c < 0xC0) ? 1 :
                          (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            int utf16Len = (c < 0x80) ? 1 : (c < 0xE0) ? 1 :
                           (c < 0xF0) ? 1 : 2;
            for (int u = 0; u < utf16Len; ++u) {
                bl.u16ToSrc.push_back(bi);
            }
            bi += utf8Len;
            si += utf8Len;
        }
        // layout is owned by the cache (don't Release it here).
        cache_->Add(bl);
    } else {
        layout->Release();
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PopAxisAlignedClip();

    if (textBrush) textBrush->Release();
    if (selBrush) selBrush->Release();
    return textY + tm.height;
}


// --- SVG block rendering ---

// Convert a UTF-32 string (from Node::raw) to UTF-8.
static std::string U32ToUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

void Renderer::ClearSvgCache() {
    for (auto& e : svg_cache_) {
        e.doc.Release();
    }
    svg_cache_.clear();
}

svg::SvgDoc* Renderer::GetSvgDoc(const Node& n, float availW) {
    if (!d2d_ctx5_) return nullptr;

    // Check cache by srcOffset
    for (auto& e : svg_cache_) {
        if (e.srcOffset == n.srcOffset) {
            return &e.doc;
        }
    }

    // Cache miss: parse the SVG
    std::string utf8 = U32ToUtf8(n.raw);
    if (utf8.empty()) return nullptr;

    SvgCacheEntry entry;
    entry.srcOffset = n.srcOffset;
    if (!entry.doc.Load(d2d_ctx5_, utf8)) {
        return nullptr;
    }
    svg_cache_.push_back(std::move(entry));
    return &svg_cache_.back().doc;
}

float Renderer::MeasureSvgBlock(IDWriteFactory* dw, const Node& n,
                                  float x, float width) {
    if (!d2d_ctx5_) return 0.0f;

    LayoutMetrics m = ComputeMetrics();
    float availW = width - 2.0f * m.codePad;

    svg::SvgDoc* doc = GetSvgDoc(n, availW);
    if (!doc || doc->Width() <= 0 || doc->Height() <= 0) return 0.0f;

    // Scale to fit available width, preserving aspect ratio.
    float scale = availW / doc->Width();
    float renderedH = doc->Height() * scale;
    return renderedH + 2.0f * m.codePad;
}

void Renderer::DrawSvgBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                             const Node& n, float x, float y, float width,
                             float& outH, const Selection* sel) {
    outH = 0.0f;
    if (!rt || !d2d_ctx5_) { outH = 0.0f; return; }

    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();
    float availW = width - 2.0f * m.codePad;

    // Edit mode: if caret is inside this block, show raw code.
    bool caretInBlock = false;
    if (sel) {
        uint32_t off = sel->active.offset;
        if (off >= n.srcOffset &&
            off < n.srcOffset + n.srcLength) {
            caretInBlock = true;
        }
    }

    if (caretInBlock) {
        // Fall back to code block rendering.
        DrawCodeBlock(rt, dw, n, x, y, width, outH, sel);
        return;
    }

    svg::SvgDoc* doc = GetSvgDoc(n, availW);
    if (!doc || doc->Width() <= 0 || doc->Height() <= 0) {
        // Parse failed, fall back to code block.
        DrawCodeBlock(rt, dw, n, x, y, width, outH, sel);
        return;
    }

    // Compute scaled dimensions.
    float scale = availW / doc->Width();
    float renderedW = doc->Width() * scale;
    float renderedH = doc->Height() * scale;
    outH = renderedH + 2.0f * m.codePad;

    // Draw background card.
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(pal.codeBg, &bgBrush);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(pal.codeBorder, &borderBrush);

    D2D1_RECT_F bgRect = D2D1::RectF(x, y, x + width, y + outH);
    if (m.codeRadius > 0.5f) {
        D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bgRect,
            m.codeRadius, m.codeRadius);
        if (bgBrush) rt->FillRoundedRectangle(rrect, bgBrush);
        if (borderBrush) rt->DrawRoundedRectangle(rrect, borderBrush, 1.0f);
    } else {
        if (bgBrush) rt->FillRectangle(bgRect, bgBrush);
        if (borderBrush) rt->DrawRectangle(bgRect, borderBrush, 1.0f);
    }
    if (bgBrush) bgBrush->Release();
    if (borderBrush) borderBrush->Release();

    // Draw the SVG document.
    doc->Draw(d2d_ctx5_, dw, x + m.codePad, y + m.codePad,
              renderedW, renderedH);
}



// -----------------------------------------------------------------------------
// Mermaid flowchart rendering (Task 12).
// -----------------------------------------------------------------------------

// Dotted edge dash pattern (D2D1_DASH_STYLE_CUSTOM takes float pairs:
// dash, gap in stroke-width multiples). Mermaid's `.flowchart .edgePath
// [style*='dotted']` renders ~3:3 dots at 1px stroke; we use 1.5/2.2 to read
// as dots at 1.5px stroke.
static const float kMermaidDash[] = {1.0f, 2.0f};
static const UINT32 kMermaidDashSize = 2;

// Note: layout is computed once at parse time (parser.cpp) and stored on
// Node::mermaid_layout, so we never re-run layout on WM_PAINT. Height is
// derived through mermaid::MeasureLayoutHeight so measure and paint agree.
float Renderer::MeasureMermaidBlock(const Node& n) const {
    if (!n.mermaid_layout) return 0.0f;
    return mermaid::MeasureLayoutHeight(*n.mermaid_layout, zoom_);
}

static void DrawArrowHead(ID2D1RenderTarget* rt, ID2D1Factory* fac,
                          ID2D1SolidColorBrush* brush,
                          const mermaid::Point& a, const mermaid::Point& b,
                          float ox, float oy, float scale) {
    if (!rt || !fac || !brush) return;
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    dx /= len; dy /= len;
    const float head_len = 10.0f;
    const float head_w = 6.0f;
    D2D1_POINT_2F tip = {
        ox + static_cast<float>(b.x * scale),
        oy + static_cast<float>(b.y * scale)
    };
    double bx = b.x - dx * (head_len / (scale > 1e-6 ? scale : 1.0f));
    double by = b.y - dy * (head_len / (scale > 1e-6 ? scale : 1.0f));
    // Perpendicular in world units.
    double px = -dy;
    double py = dx;
    double half = head_w / (scale > 1e-6 ? scale : 1.0f);
    D2D1_POINT_2F left = {
        ox + static_cast<float>((bx + px * half) * scale),
        oy + static_cast<float>((by + py * half) * scale)
    };
    D2D1_POINT_2F right = {
        ox + static_cast<float>((bx - px * half) * scale),
        oy + static_cast<float>((by - py * half) * scale)
    };
    ID2D1PathGeometry* geom = nullptr;
    if (FAILED(fac->CreatePathGeometry(&geom)) || !geom) return;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geom->Open(&sink)) || !sink) { geom->Release(); return; }
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    D2D1_POINT_2F pts[2] = { left, right };
    sink->AddLines(pts, 2);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    rt->FillGeometry(geom, brush);
    geom->Release();
}

void Renderer::DrawMermaidBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                 const Node& n, float x, float y) {
    if (!rt || !dw) return;
    if (!n.mermaid_layout) return;
    const auto& lo = *n.mermaid_layout;
    const float pad = mermaid::kMermaidBlockPad;
    float scale = zoom_;
    float ox = x;
    float oy = y + pad;

    Palette pal = BasePalette();
    ID2D1SolidColorBrush* edgeBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &edgeBrush);
    ID2D1SolidColorBrush* nodeFill = nullptr;
    rt->CreateSolidColorBrush(pal.codeBg, &nodeFill);
    ID2D1SolidColorBrush* nodeBorder = nullptr;
    rt->CreateSolidColorBrush(pal.codeBorder, &nodeBorder);
    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);

    ID2D1Factory* fac = nullptr;
    rt->GetFactory(&fac);

    // Swimlane bands first, so nodes/edges overlay.
    if (!lo.lanes.empty()) {
        ID2D1SolidColorBrush* laneFill = nullptr;
        ID2D1SolidColorBrush* laneBorder = nullptr;
        ID2D1SolidColorBrush* laneTitle = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f), &laneFill);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.60f, 0.60f, 1.0f), &laneBorder);
        rt->CreateSolidColorBrush(pal.textPrimary, &laneTitle);
        for (const auto& lb : lo.lanes) {
            float lx = ox + static_cast<float>(lb.x * scale);
            float ly = oy + static_cast<float>(lb.y * scale);
            float lw = static_cast<float>(lb.width  * scale);
            float lh = static_cast<float>(lb.height * scale);
            D2D1_RECT_F r = D2D1::RectF(lx, ly, lx + lw, ly + lh);
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6.0f * scale, 6.0f * scale);
            if (laneFill)   rt->FillRoundedRectangle(rr, laneFill);
            if (laneBorder) rt->DrawRoundedRectangle(rr, laneBorder, 1.0f);

            if (!lb.title.empty() && body_fmt_ && laneTitle) {
                std::u16string t16;
                for (unsigned char c : lb.title) t16.push_back(static_cast<char16_t>(c));
                IDWriteTextLayout* tl = nullptr;
                if (SUCCEEDED(dw->CreateTextLayout(
                        reinterpret_cast<const WCHAR*>(t16.data()),
                        static_cast<UINT32>(t16.size()),
                        body_fmt_, lw, 20.0f * scale, &tl)) && tl) {
                    rt->DrawTextLayout(D2D1::Point2F(lx + 6.0f * scale, ly + 2.0f * scale),
                                       tl, laneTitle,
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    tl->Release();
                }
            }
        }
        if (laneFill)   laneFill->Release();
        if (laneBorder) laneBorder->Release();
        if (laneTitle)  laneTitle->Release();
    }

    // Edges first.
    if (fac && edgeBrush) {
        ID2D1SolidColorBrush* labelBrush = nullptr;
        rt->CreateSolidColorBrush(pal.textPrimary, &labelBrush);
        for (const auto& e : lo.edges) {
            if (e.route.size() < 2) continue;
            // Stroke style: dotted edges use a dash pattern; thick edges
            // draw at ~2.5x width, matching mermaid's visual weight.
            const bool dotted = (e.style == mermaid::LineStyle::Dotted);
            const bool thick = (e.style == mermaid::LineStyle::Thick);
            const float stroke_w = thick ? 3.5f : 1.5f;
            ID2D1StrokeStyle* stroke = nullptr;
            if (dotted && fac) {
                // Square dashes, like mermaid's stroke-dasharray 3 3 look.
                fac->CreateStrokeStyle(
                    D2D1_STROKE_STYLE_PROPERTIES{
                        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_MITER, 8.0f,
                        D2D1_DASH_STYLE_CUSTOM, 0.0f,
                    },
                    kMermaidDash, kMermaidDashSize, &stroke);
            }
            ID2D1PathGeometry* path = nullptr;
            if (FAILED(fac->CreatePathGeometry(&path)) || !path) { if (stroke) stroke->Release(); continue; }
            ID2D1GeometrySink* sink = nullptr;
            if (FAILED(path->Open(&sink)) || !sink) { path->Release(); if (stroke) stroke->Release(); continue; }
            D2D1_POINT_2F p0 = {
                ox + static_cast<float>(e.route[0].x * scale),
                oy + static_cast<float>(e.route[0].y * scale)
            };
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < e.route.size(); ++i) {
                D2D1_POINT_2F pi = {
                    ox + static_cast<float>(e.route[i].x * scale),
                    oy + static_cast<float>(e.route[i].y * scale)
                };
                sink->AddLine(pi);
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            rt->DrawGeometry(path, edgeBrush, stroke_w, stroke);
            path->Release();
            if (stroke) stroke->Release();
            if (e.head == mermaid::Head::Arrow) {
                DrawArrowHead(rt, fac, edgeBrush,
                              e.route[e.route.size() - 2], e.route.back(),
                              ox, oy, scale);
            }

            // Edge label: centered at the polyline midpoint, with an opaque
            // backing rect so the line does not strike through the glyphs.
            if (!e.label.empty() && labelBrush && body_fmt_) {
                size_t mid_seg = (e.route.size() - 1) / 2;
                double mx = (e.route[mid_seg].x + e.route[mid_seg + 1].x) * 0.5;
                double my = (e.route[mid_seg].y + e.route[mid_seg + 1].y) * 0.5;
                std::u16string m16;
                {
                    const std::string& s = e.label;
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
                            m16.push_back(static_cast<char16_t>(cp));
                        } else {
                            cp -= 0x10000;
                            m16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                            m16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                        }
                        i += n_b;
                    }
                }
                IDWriteTextLayout* tl = nullptr;
                float max_lbl_w = 220.0f * scale;
                if (SUCCEEDED(dw->CreateTextLayout(
                        reinterpret_cast<const WCHAR*>(m16.data()),
                        static_cast<UINT32>(m16.size()),
                        body_fmt_, max_lbl_w, 28.0f * scale, &tl)) && tl) {
                    DWRITE_TEXT_METRICS tm{};
                    tl->GetMetrics(&tm);
                    float lbl_w = tm.widthIncludingTrailingWhitespace;
                    float lbl_h = tm.height;
                    float lx = ox + static_cast<float>(mx * scale) - lbl_w * 0.5f;
                    float ly = oy + static_cast<float>(my * scale) - lbl_h * 0.5f;
                    D2D1_RECT_F bg = D2D1::RectF(lx - 2.0f, ly - 2.0f,
                                                 lx + lbl_w + 2.0f, ly + lbl_h + 2.0f);
                    ID2D1SolidColorBrush* bgBrush = nullptr;
                    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &bgBrush);
                    if (bgBrush) {
                        rt->FillRectangle(bg, bgBrush);
                        bgBrush->Release();
                    }
                    rt->DrawTextLayout(D2D1::Point2F(lx, ly), tl, labelBrush,
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    tl->Release();
                }
            }
        }
        if (labelBrush) labelBrush->Release();
    }

    // Nodes.
    for (const auto& nd : lo.nodes) {
        if (nd.is_dummy) continue;
        float nx = ox + static_cast<float>((nd.x - nd.width / 2.0f) * scale);
        float ny = oy + static_cast<float>((nd.y - nd.height / 2.0f) * scale);
        float nw = static_cast<float>(nd.width * scale);
        float nh = static_cast<float>(nd.height * scale);
        D2D1_RECT_F rect = D2D1::RectF(nx, ny, nx + nw, ny + nh);

        switch (nd.shape) {
            case mermaid::NodeShape::Round: {
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 6.0f * scale, 6.0f * scale);
                if (nodeFill) rt->FillRoundedRectangle(rr, nodeFill);
                if (nodeBorder) rt->DrawRoundedRectangle(rr, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Stadium: {
                float r = nh * 0.5f;
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, r, r);
                if (nodeFill) rt->FillRoundedRectangle(rr, nodeFill);
                if (nodeBorder) rt->DrawRoundedRectangle(rr, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Circle: {
                D2D1_ELLIPSE el = D2D1::Ellipse(
                    D2D1::Point2F(nx + nw * 0.5f, ny + nh * 0.5f),
                    nw * 0.5f, nh * 0.5f);
                if (nodeFill) rt->FillEllipse(el, nodeFill);
                if (nodeBorder) rt->DrawEllipse(el, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Diamond: {
                if (fac) {
                    ID2D1PathGeometry* geom = nullptr;
                    if (SUCCEEDED(fac->CreatePathGeometry(&geom)) && geom) {
                        ID2D1GeometrySink* sink = nullptr;
                        if (SUCCEEDED(geom->Open(&sink)) && sink) {
                            D2D1_POINT_2F top = D2D1::Point2F(nx + nw * 0.5f, ny);
                            D2D1_POINT_2F right = D2D1::Point2F(nx + nw, ny + nh * 0.5f);
                            D2D1_POINT_2F bot = D2D1::Point2F(nx + nw * 0.5f, ny + nh);
                            D2D1_POINT_2F left = D2D1::Point2F(nx, ny + nh * 0.5f);
                            sink->BeginFigure(top, D2D1_FIGURE_BEGIN_FILLED);
                            D2D1_POINT_2F pts[3] = { right, bot, left };
                            sink->AddLines(pts, 3);
                            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                            sink->Close();
                            sink->Release();
                            if (nodeFill) rt->FillGeometry(geom, nodeFill);
                            if (nodeBorder) rt->DrawGeometry(geom, nodeBorder, 1.0f);
                        }
                        geom->Release();
                    }
                }
                break;
            }
            case mermaid::NodeShape::Rect:
            default: {
                if (nodeFill) rt->FillRectangle(rect, nodeFill);
                if (nodeBorder) rt->DrawRectangle(rect, nodeBorder, 1.0f);
                break;
            }
        }

        // Label text, centered. Mermaid line-break separators (<br/> etc.)
        // split into separate lines; the DWrite layout renders them as a
        // multi-line block that matches SplitLabelLines sizing.
        if (!nd.label.empty() && body_fmt_ && textBrush) {
            std::vector<std::string> lines =
                mermaid::SplitLabelLines(nd.label);
            if (lines.empty()) lines.push_back(nd.label);
            std::u16string text16;
            for (size_t li = 0; li < lines.size(); ++li) {
                if (li > 0) text16.push_back(u'\n');
                const std::string& s = lines[li];
                for (size_t i = 0; i < s.size(); ) {
                    unsigned char c = static_cast<unsigned char>(s[i]);
                    uint32_t cp = 0;
                    int n_bytes = 1;
                    if (c < 0x80) { cp = c; n_bytes = 1; }
                    else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                        cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F);
                        n_bytes = 2;
                    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                        cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) |
                             (s[i+2] & 0x3F);
                        n_bytes = 3;
                    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
                        cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                             ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
                        n_bytes = 4;
                    } else { cp = '?'; n_bytes = 1; }
                    if (cp <= 0xFFFF) {
                        text16.push_back(static_cast<char16_t>(cp));
                    } else {
                        cp -= 0x10000;
                        text16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                        text16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                    }
                    i += n_bytes;
                }
            }
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, nw, nh, &layout);
            if (SUCCEEDED(hr) && layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(nx, ny), layout, textBrush,
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                layout->Release();
            }
        }
    }

    if (fac) fac->Release();
    if (edgeBrush) edgeBrush->Release();
    if (nodeFill) nodeFill->Release();
    if (nodeBorder) nodeBorder->Release();
    if (textBrush) textBrush->Release();
}
