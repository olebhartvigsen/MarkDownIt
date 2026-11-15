#include <array>

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
                                              const LayoutMetrics& m,
                                              bool monospace) {
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
        // A data table puts every cell in the fixed face, header and body
        // alike, so a column of figures is read down one edge. A prose table
        // keeps the body face and takes the code face only for code spans.
        const float size =
            (monospace || code) ? m.codeFontSize : m.bodyFontSize;
        const char* family =
            monospace ? kCodeFont
                      : (strong ? kSemiboldBodyFont
                                : (code ? kCodeFont : nullptr));
        glyph.font = fontSpecific(size, strong && (monospace || code), em,
                                  monospace || code, family);
        glyph.r = color.r; glyph.g = color.g;
        glyph.b = color.b; glyph.a = color.a;
        glyph.underline = link && m.linkUnderline; glyph.strike = strike;
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
                     in.metrics->codeFontSize, in.palette->link,
                     in.metrics->linkUnderline);
}

// The widest unbreakable token of one cell, the width its column
// must carry before the word stops slicing at the cell edge. A run
// carries one style; its words split at the whitespace the walker
// breaks lines at, each token measured by the engine with the
// run's own face so weight, code size and zoom all count.
float WidestToken(const std::vector<platform::GlyphRun>& runs,
                  const LayoutInput& in) {
    float widest = 0.0f;
    for (const platform::GlyphRun& run : runs) {
        const std::string& text = run.text;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            const bool end = i == text.size() ||
                             text[i] == ' ' || text[i] == '\t';
            if (!end) continue;
            if (i > start) {
                std::vector<platform::GlyphRun> one;
                platform::GlyphRun token = run;
                token.text = text.substr(start, i - start);
                one.push_back(std::move(token));
                std::vector<platform::TextLine> lines =
                    in.engine->MeasureWrapped(one, 1.0e9f, 0.0f);
                for (const platform::TextLine& line : lines)
                    widest = (std::max)(widest, line.width);
            }
            start = i + 1;
        }
    }
    return widest;
}

}  // namespace

std::vector<float> DistributeTableColumns(
    const std::vector<float>& minInner, float contentWidth,
    size_t columnCount, float cellPadX) {
    std::vector<float> colWs(columnCount, 0.0f);
    if (columnCount == 0 || contentWidth <= 0.0f) return colWs;
    const float equal =
        contentWidth / static_cast<float>(columnCount);
    std::vector<float> need(columnCount, 0.0f);
    bool anyTight = false;
    float sumNeed = 0.0f;
    for (size_t c = 0; c < columnCount; ++c) {
        const float inner =
            c < minInner.size() ? minInner[c] : 0.0f;
        need[c] = inner + 2.0f * cellPadX;
        sumNeed += need[c];
        if (need[c] > equal) anyTight = true;
    }
    if (!anyTight) {
        for (float& w : colWs) w = equal;
        return colWs;
    }
    if (sumNeed <= contentWidth) {
        // Fixpoint: a column whose longest word needs more than the
        // running share takes its need; the rest keep sharing what
        // is left, never below their own need.
        std::vector<char> tight(columnCount, 0);
        float free = contentWidth;
        size_t flex = columnCount;
        for (size_t round = 0; round < columnCount; ++round) {
            const float share = free / static_cast<float>(flex);
            bool moved = false;
            for (size_t c = 0; c < columnCount; ++c) {
                if (tight[c] || need[c] <= share) continue;
                tight[c] = 1;
                colWs[c] = need[c];
                free -= need[c];
                --flex;
                moved = true;
            }
            if (!moved || flex == 0) break;
        }
        if (flex > 0) {
            const float share = free / static_cast<float>(flex);
            for (size_t c = 0; c < columnCount; ++c)
                if (!tight[c]) colWs[c] = share;
        } else {
            for (size_t c = 0; c < columnCount; ++c)
                if (colWs[c] <= 0.0f) colWs[c] = 0.0f;
        }
        return colWs;
    }
    // Degenerate: the words alone outweigh the table. Share the
    // deficit pro rata so every column clips a little instead of
    // one column eating the whole frame.
    if (sumNeed > 0.0f) {
        const float scale = contentWidth / sumNeed;
        for (size_t c = 0; c < columnCount; ++c)
            colWs[c] = need[c] * scale;
    }
    return colWs;
}

float TableColumnX(const LaidTable& t, size_t column) {
    if (column < t.colXs.size()) return t.colXs[column];
    return static_cast<float>(column) * t.colW;
}

float TableColumnW(const LaidTable& t, size_t column) {
    if (column < t.colWs.size()) return t.colWs[column];
    return t.colW;
}

platform::Color PinnedHeaderFill(const LayoutMetrics& metrics,
                                 const Palette& palette, bool dataTable) {
    // The QA walk dresses the band in a colour no theme uses, so a capture
    // run can answer where it landed: the pinned band is otherwise nearly
    // the paper's own colour and reads as nothing in a screenshot.
    if (const char* qa = std::getenv("MARKDOWNIT_QA_BAND")) {
        if (*qa) return platform::Color{0.10f, 0.75f, 0.55f, 1.0f};
    }
    if (dataTable || metrics.tableHeaderFill) {
        return ToColor(dataTable ? palette.tableRowAlt : palette.tableHeaderBg);
    }
    return ToColor(palette.tableRowAlt);
}

std::array<platform::Point, 6> FilterGlyph(float cx, float cy, float mark) {
    // A funnel: wide mouth, narrow stem, six points, built around the
    // centre so a painter that redraws the band draws the same mark.
    const float halfTop = mark * 0.5f;
    const float halfStem = mark * 0.16f;
    const float midY = cy - mark * 0.10f;
    const float bottom = cy + mark * 0.55f;
    return std::array<platform::Point, 6>{
        platform::Point{cx - halfTop, cy - mark * 0.42f},
        platform::Point{cx + halfTop, cy - mark * 0.42f},
        platform::Point{cx + halfStem, midY},
        platform::Point{cx + halfStem, bottom},
        platform::Point{cx - halfStem, bottom},
        platform::Point{cx - halfStem, midY},
    };
}

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
    // The column split: a column must carry its longest unbreakable
    // word instead of slicing it at the cell edge, so the words are
    // token measured first and the widths follow the demand.
    std::vector<float> minInner(columnCount, 0.0f);
    for (const TableRow& row : node.rows) {
        const size_t here = (std::min)(row.cells.size(), columnCount);
        for (size_t c = 0; c < here; ++c) {
            const TableCell& cell = row.cells[c];
            std::vector<platform::GlyphRun> runs =
                BuildCellRuns(cell, cell.isHeader,
                              ToColor(in.palette->textPrimary), m,
                              node.tableMonospace);
            if (runs.empty()) runs = PlaceholderRuns(cell.isHeader, in);
            // A header cell gives the filter button room at its right
            // edge, so a long name wraps earlier instead of running under
            // the mark.
            const float gutter = cell.isHeader && !node.tableFilterActive.empty()
                                     ? m.tableFilterGutter
                                     : 0.0f;
            const float token = WidestToken(runs, in) + gutter;
            if (token > minInner[c]) minInner[c] = token;
        }
    }
    t.colWs = DistributeTableColumns(minInner, in.contentWidth,
                                     columnCount, m.cellPadX);
    t.colXs.assign(columnCount + 1, 0.0f);
    {
        float x = 0.0f;
        for (size_t c = 0; c < columnCount; ++c) {
            t.colXs[c] = x;
            x += t.colWs[c];
        }
        t.colXs[columnCount] = x;
    }

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
                              ToColor(in.palette->textPrimary), m,
                              node.tableMonospace);
            if (runs.empty()) runs = PlaceholderRuns(cell.isHeader, in);
            const float gutter = cell.isHeader && !node.tableFilterActive.empty()
                                     ? m.tableFilterGutter
                                     : 0.0f;
            const float innerW =
                t.colWs[c] - 2.0f * m.cellPadX - gutter;
            std::vector<platform::TextLine> lines =
                in.engine->MeasureWrapped(
                    runs, innerW > 0.0f ? innerW : 1.0f, 0.0f);
            ApplyLineSpacing(&lines, m.bodyFontSize, m.bodyLineHeight,
                             in.zoom, in.engine);
            LaidTableCell lc;
            for (const platform::TextLine& line : lines)
                lc.height += line.height;
            if (lc.height > tallest) tallest = lc.height;
            lc.x = t.colXs[c]; lc.width = t.colWs[c];
            lc.y = rowTop;
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
    // Fills use the document's y. Starting at the table's top keeps the
    // grid on its own rows; starting at 0 painted it over the heading
    // and the paragraphs above.
    const float top = *y;
    // The header row's own items are emitted twice: once into the page and
    // once into the band a scrolling view pins to its top edge. Both copies
    // are in document coordinates, so the pin is a translate and nothing has
    // to be re-measured when the reader scrolls.
    const bool headerRowPresent = !t.rowHeights.empty() &&
                                  !node.rows[0].cells.empty() &&
                                  node.rows[0].cells[0].isHeader;
    t.headerHeight = headerRowPresent ? t.rowHeights[0] : 0.0f;
    const auto emit = [&](const display::Item& item, bool intoHeader) {
        ctx.list->items.push_back(item);
        if (intoHeader && headerRowPresent) t.header.items.push_back(item);
    };
    const auto fill = [&](float fx, float fy, float fw, float fh,
                          const platform::Color& color, bool intoHeader) {
        emit(display::FillRect{platform::Rect{fx, fy, fw, fh}, color},
             intoHeader);
    };
    // The row washes, full width. GitHub: the header is the page's
    // own paper and every second body row carries the muted fill.
    // Wikipedia's wikitable: every body row carries the neutral fill
    // and the header its darker step.
    rowTop = top;
    for (size_t r = 0; r < t.rowHeights.size(); ++r) {
        const float rowH = t.rowHeights[r];
        const bool headerRow = !node.rows[r].cells.empty() &&
                               node.rows[r].cells[0].isHeader;
        // A data table always marks its header: a column of figures with no
        // header line reads as one long list, and the filter buttons need a
        // row that looks like one. A prose table keeps its style's rule.
        const bool dataTable = node.tableMonospace;
        if (headerRow) {
            if (m.tableHeaderFill || dataTable)
                fill(0.0f, rowTop, in.contentWidth, rowH,
                     ToColor(dataTable ? in.palette->tableRowAlt
                                       : in.palette->tableHeaderBg),
                     r == 0);
        } else if (!dataTable &&
                   (m.tableBodyFill || (m.tableZebra && r % 2 == 1))) {
            fill(0.0f, rowTop, in.contentWidth, rowH,
                 ToColor(in.palette->tableRowAlt), false);
        }
        rowTop += rowH;
    }

    // The hairline grid, one DIP wide unzoomed: the first row's top,
    // every row's bottom, one vertical per column boundary, the last
    // pulled a DIP inside the right edge.
    const platform::Color grid = ToColor(in.palette->tableBorder);
    rowTop = top;
    for (size_t r = 0; r < t.rowHeights.size(); ++r) {
        const float rowH = t.rowHeights[r];
        if (r == 0 && m.tableVerticalLines)
            fill(0.0f, rowTop, in.contentWidth, 1.0f, grid, true);
        fill(0.0f, rowTop + rowH - 1.0f, in.contentWidth, 1.0f, grid,
             r == 0);
        for (size_t c = 0; c <= t.columnCount && m.tableVerticalLines; ++c) {
            float gx = TableColumnX(t, c);
            if (c >= t.columnCount) gx = in.contentWidth - 1.0f;
            fill(gx, rowTop, 1.0f, rowH, grid, r == 0);
        }
        rowTop += rowH;
    }

    // The cells carry no replay text any more. The record keeps
    // their measured lines for the caret, the washes and the per
    // port overlay painters, and each port paints the lines from
    // that one record, placed by the column's alignment and the
    // row's own centering. The replay used to draw a second copy
    // beside the overlay's glyphs, and the two disagreed on line
    // breaks in cells with mixed fonts: the pieces landed inside
    // each other and the table read as glued. One painter, one
    // geometry, one place.
    // The filter buttons: one funnel at each header column's right edge,
    // drawn after the washes and the grid so it sits on both. A column the
    // reader has filtered takes the link color, so a filtered table says
    // which of its columns is doing the work.
    // The pinned band's own surface, under everything else in the copy. The
    // page keeps whatever the style does; the band has to be opaque because
    // the body goes under it.
    if (headerRowPresent) {
        t.header.items.insert(
            t.header.items.begin(),
            display::FillRect{
                platform::Rect{0.0f, top, in.contentWidth, t.headerHeight},
                PinnedHeaderFill(m, *in.palette, node.tableMonospace)});
    }

    if (!node.tableFilterActive.empty() && !t.rowHeights.empty()) {
        const size_t columns = (std::min)(t.columnCount,
                                          node.tableFilterActive.size());
        const float mark = m.tableFilterMark;
        const platform::Color idle = ToColor(in.palette->textMuted);
        const platform::Color active = ToColor(in.palette->link);
        t.filterButtons.assign(t.columnCount, platform::Rect{});
        for (size_t c = 0; c < columns; ++c) {
            if (c >= t.colWs.size()) break;
            const float right = TableColumnX(t, c) + t.colWs[c] - m.cellPadX;
            const float cx = right - mark * 0.5f;
            const float cy = top + t.rowHeights[0] * 0.5f;
            const std::array<platform::Point, 6> points =
                FilterGlyph(cx, cy, mark);
            display::FillPolygon funnel;
            funnel.points.assign(points.begin(), points.end());
            funnel.color = node.tableFilterActive[c] ? active : idle;
            emit(funnel, true);
            // The target runs the header row's whole height and reaches
            // further in than the mark, so a reader does not have to hit
            // seven points of ink.
            const float hit = (std::max)(m.tableFilterHit,
                                         m.tableFilterGutter);
            t.filterButtons[c] = platform::Rect{
                TableColumnX(t, c) + t.colWs[c] - hit,
                top, hit, t.rowHeights[0]};
            t.filterCenters.push_back(platform::Point{cx, cy});
        }
    }

    *y += total;
    block.y = *y - total;
    return block;
}

std::vector<platform::Rect> FilterButtons(const DocumentLayout& laid) {
    std::vector<platform::Rect> buttons;
    for (const LaidBlock& block : laid.blocks) {
        if (!block.table) continue;
        for (const platform::Rect& rect : block.table->filterButtons) {
            buttons.push_back(rect);
        }
    }
    return buttons;
}

bool PinnedHeader(float blockTop, float blockHeight, float headerHeight,
                  float viewportTop, float* top) {
    if (headerHeight <= 0.0f) return false;
    // The table has to cross the edge: a header still on screen is drawn
    // where it was measured, and a table below the edge has nothing to pin.
    if (blockTop >= viewportTop) return false;
    if (blockTop + blockHeight <= viewportTop) return false;
    if (top) *top = viewportTop;
    return true;
}

bool FilterButtonAt(const std::vector<platform::Rect>& buttons, float docX,
                    float docY, size_t* column) {
    for (size_t c = 0; c < buttons.size(); ++c) {
        const platform::Rect& r = buttons[c];
        if (docX >= r.x && docX <= r.x + r.width && docY >= r.y &&
            docY <= r.y + r.height) {
            if (column) *column = c;
            return true;
        }
    }
    return false;
}

}  // namespace layout
