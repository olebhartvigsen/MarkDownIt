// The shared layout of a markdown table, plan Batch 3.2 step 6.
// The two renderers measured a table up to three times: the height
// walk, the per-cell geometry walk and the cell paint walk. This
// module measures each cell exactly once, records what the caret
// layer needs in the LaidTable, then emits the display items from
// that record. Columns split equally, the rule both renderers apply.

#include "layout/document_layout.h"
#include "layout/text_runs.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace layout {
namespace {

// The runs of one cell, ported from the renderers' cell builders:
// CellInlineSpan ranges index UTF-16 units, so they are counted over
// the UTF-32 text. Header cells and strong spans go to the semibold
// face, code spans to the code face at the code size, links underline.
std::vector<platform::GlyphRun> BuildCellRuns(const TableCell& cell,
                                              bool bold,
                                              const platform::Color& color,
                                              const LayoutMetrics& m) {
    std::vector<platform::GlyphRun> runs;
    const std::u32string& text = cell.text;
    const size_t count = text.size();
    auto styleAt = [&](uint32_t i, bool* strong, bool* em, bool* code,
                       bool* strike) {
        *strong = bold; *em = false; *code = false; *strike = false;
        for (const CellInlineSpan& span : cell.inlineSpans) {
            if (i < span.u16Start || i >= span.u16End) continue;
            *strong = *strong || span.bold;
            *em = *em || span.italic;
            *code = *code || span.code;
            *strike = *strike || span.strike;
        }
    };
    auto isLink = [&](uint32_t i) {
        if (i >= cell.u16ToSrc.size()) return false;
        const uint32_t src = cell.u16ToSrc[i];
        for (const TableLink& link : cell.links)
            if (src >= link.srcOffset && src < link.srcOffset + link.srcLength)
                return true;
        return false;
    };
    uint32_t u16 = 0; size_t i = 0;
    while (i < count) {
        bool strong = false, em = false, code = false, strike = false;
        styleAt(u16, &strong, &em, &code, &strike);
        const bool link = isLink(u16);
        std::string run;  // it takes every code point of one style
        while (i < count) {
            bool nS = false, nE = false, nC = false, nK = false;
            styleAt(u16, &nS, &nE, &nC, &nK);
            if (nS != strong || nE != em || nC != code || nK != strike ||
                isLink(u16) != link)
                break;
            run += Utf32ToUtf8(std::u32string(1, text[i]));
            u16 += text[i] > 0xFFFF ? 2u : 1u;
            ++i;
        }
        if (run.empty()) continue;
        platform::GlyphRun glyph;
        glyph.text = std::move(run);
        const float size = code ? m.codeFontSize : m.bodyFontSize;
        const char* family =
            strong ? kSemiboldBodyFont : (code ? kCodeFont : nullptr);
        glyph.font = fontSpecific(size, code && strong, em, code, family);
        glyph.r = color.r; glyph.g = color.g;
        glyph.b = color.b; glyph.a = color.a;
        glyph.underline = link; glyph.strike = strike;
        runs.push_back(std::move(glyph));
    }
    return runs;
}

// The placeholder of an empty cell: one space of the body size, the
// run both renderers substitute so the caret has a rect inside it.
std::vector<platform::GlyphRun> PlaceholderRuns(
        bool header, const LayoutInput& in) {
    Node node;
    InlineBlock child;
    child.text = U" ";
    node.children.push_back(child);
    return BuildRuns(node, in.metrics->bodyFontSize, header, false,
                     ToColor(in.palette->textPrimary), false, nullptr,
                     in.metrics->codeFontSize, in.palette->link);
}

}  // namespace

LaidBlock LayoutTableBlock(const LayoutCtx& ctx, const Node& node,
                           uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;
    LaidBlock block;
    block.kind = BlockKind::Table; block.nodeIndex = nodeIndex;
    block.x = 0.0f; block.width = in.contentWidth;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;
    block.table = std::make_shared<LaidTable>();
    LaidTable& t = *block.table;

    // The column count is the widest row, every row padded to it.
    size_t columnCount = 0;
    for (const TableRow& row : node.rows)
        columnCount = (std::max)(columnCount, row.cells.size());
    if (columnCount == 0) {  // no columns anywhere, nothing to charge
        block.y = *y;
        return block;
    }
    t.columnCount = columnCount;
    t.colW = in.contentWidth / static_cast<float>(columnCount);
    const float innerW = t.colW - 2.0f * m.cellPadX;

    // The one measurement walk: each cell is measured once and the
    // record feeds the row heights, caret cells and paint items below.
    float total = 0.0f;
    float rowTop = 0.0f;
    for (const TableRow& row : node.rows) {
        float tallest = 0.0f;
        std::vector<LaidTableCell> laid;
        const size_t here = (std::min)(row.cells.size(), columnCount);
        for (size_t c = 0; c < here; ++c) {
            const TableCell& cell = row.cells[c];
            std::vector<platform::GlyphRun> runs =
                BuildCellRuns(cell, cell.isHeader,
                              ToColor(in.palette->textPrimary), m);
            if (runs.empty()) runs = PlaceholderRuns(cell.isHeader, in);
            std::vector<platform::TextLine> lines =
                in.engine->MeasureWrapped(
                    runs, innerW > 0.0f ? innerW : 1.0f, 0.0f);
            ApplyLineSpacing(&lines, m.bodyFontSize, m.bodyLineHeight,
                             in.zoom, in.engine);
            LaidTableCell lc;
            for (const platform::TextLine& line : lines)
                lc.height += line.height;
            if (lc.height > tallest) tallest = lc.height;
            lc.x = static_cast<float>(c) * t.colW;
            lc.y = rowTop; lc.width = t.colW;
            lc.srcCellStart = cell.srcOffset; lc.srcCellEnd = cell.srcEnd;
            lc.lines = std::move(lines);
            laid.push_back(std::move(lc));
        }
        // A row with no text keeps one body line, the renderers' fix.
        if (tallest == 0.0f)
            tallest = m.bodyLineHeight * m.bodyFontSize * kPtToDip * in.zoom;
        const float rowH = tallest + 2.0f * m.cellPadY;
        for (LaidTableCell& lc : laid) lc.height = rowH;
        t.rowHeights.push_back(rowH);
        t.cells.push_back(std::move(laid));
        total += rowH;
        rowTop += rowH;
    }
    block.height = total;

    // Paint order: zebra wash, hairline grid, clipped cell text. The
    // mac interleaves these per row, but the hairlines sit exactly on
    // the cell edges while the text is clipped a cellPadX inside, so
    // nothing overlaps and the layer order is the z order.
    const auto fill = [&](float fx, float fy, float fw, float fh,
                          const platform::Color& color) {
        ctx.list->items.push_back(
            display::FillRect{platform::Rect{fx, fy, fw, fh}, color});
    };
    // GitHub's stripe, full width: the header is the page's own paper,
    // every second body row carries the muted fill.
    rowTop = 0.0f;
    for (size_t r = 0; r < t.rowHeights.size(); ++r) {
        const float rowH = t.rowHeights[r];
        const bool headerRow = !node.rows[r].cells.empty() &&
                               node.rows[r].cells[0].isHeader;
        if (!headerRow && r % 2 == 1)
            fill(0.0f, rowTop, in.contentWidth, rowH,
                 ToColor(in.palette->tableRowAlt));
        rowTop += rowH;
    }

    // The hairline grid, one DIP wide unzoomed: the first row's top,
    // every row's bottom, one vertical per column boundary, the last
    // pulled a DIP inside the right edge.
    const platform::Color grid = ToColor(in.palette->tableBorder);
    rowTop = 0.0f;
    for (size_t r = 0; r < t.rowHeights.size(); ++r) {
        const float rowH = t.rowHeights[r];
        if (r == 0) fill(0.0f, rowTop, in.contentWidth, 1.0f, grid);
        fill(0.0f, rowTop + rowH - 1.0f, in.contentWidth, 1.0f, grid);
        for (size_t c = 0; c <= t.columnCount; ++c) {
            float gx = static_cast<float>(c) * t.colW;
            if (c >= t.columnCount) gx = in.contentWidth - 1.0f;
            fill(gx, rowTop, 1.0f, rowH, grid);
        }
        rowTop += rowH;
    }

    // The cells: clip to the padded inner rect, place the lines by
    // the column's alignment; the geometry comes from the record.
    for (size_t r = 0; r < node.rows.size() && r < t.cells.size(); ++r) {
        const TableRow& row = node.rows[r];
        const float rowH = t.rowHeights[r];
        for (size_t c = 0; c < row.cells.size() && c < t.columnCount; ++c) {
            const LaidTableCell& lc = t.cells[r][c];
            const float cellX = static_cast<float>(c) * t.colW;
            display::PushClip clip;
            clip.rect.x = cellX + m.cellPadX;
            clip.rect.y = lc.y + m.cellPadY;
            clip.rect.width = innerW > 0.0f ? innerW : 0.0f;
            clip.rect.height = rowH - 2.0f * m.cellPadY;
            const bool clipped = clip.rect.width > 0.0f &&
                                 clip.rect.height > 0.0f;
            if (clipped) ctx.list->items.push_back(clip);
            float textH = 0.0f;
            for (const platform::TextLine& line : lc.lines)
                textH += line.height;
            float lineY = lc.y + (std::max)(m.cellPadY,
                                           (rowH - textH) * 0.5f);
            const ColumnAlign align = c < node.aligns.size()
                                          ? node.aligns[c]
                                          : ColumnAlign::Default;
            uint32_t src = lc.srcCellStart;
            for (const platform::TextLine& line : lc.lines) {
                float lineX = cellX + m.cellPadX;
                if (align == ColumnAlign::Center)
                    lineX += (innerW - line.width) * 0.5f;
                else if (align == ColumnAlign::Right)
                    lineX += innerW - line.width;
                display::Text item;
                item.x = lineX;
                item.top = lineY;
                item.line = line;
                item.srcOffset = src;
                for (const platform::GlyphRun& run : line.runs)
                    item.srcLength += static_cast<uint32_t>(run.text.size());
                src += item.srcLength;
                ctx.list->items.push_back(item);
                lineY += line.height;
            }
            if (clipped) ctx.list->items.push_back(display::PopClip{});
        }
    }
    *y += total;
    block.y = *y - total;
    return block;
}

}  // namespace layout
