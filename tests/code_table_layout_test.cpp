// The golden tests of the shared code block and table layout, plan
// Batch 3.2 steps 5 and 6. The fake engine draws every code point at
// 8 DIP, so the numbers hold on both runners or the layout drifted.
// A fake run never breaks inside itself, so a fenced block's one
// run wraps at the unchanged width, exactly what the renderers see.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"

#include <variant>
#include <vector>

namespace {

#define NEAR(a, b) EXPECT_NEAR(a, b, 0.001f)
const float kPt = 96.0f / 72.0f;
const LayoutMetrics kM = BaseMetrics();
const Palette kPal = BasePalette();
// One body line of the theme metrics, the table row height unit.
const float kBodyLine = kM.bodyLineHeight * kM.bodyFontSize * kPt;
using display::PushClip;
using display::Text;
// The checked display::Item alternatives, by variant index.
constexpr size_t kFill = 0, kCard = 1, kText = 11, kClip = 13, kPop = 14;

// The item at an index, as its expected type.
template <typename T>
const T& At(const display::DisplayList& l, size_t i) {
    return std::get<T>(l.items[i]);
}

// A cell of the fixture, with its source byte range.
TableCell Cell(const char32_t* text, bool header, uint32_t src,
               uint32_t end) {
    TableCell c;
    if (text) c.text = text;
    c.isHeader = header; c.srcOffset = src; c.srcEnd = end;
    return c;
}

// One fake engine bound to one list, at a given column width.
struct Rig {
    fake::FakeTextEngine engine;
    layout::LayoutInput in;
    display::DisplayList list;
    layout::LayoutCtx ctx;
    explicit Rig(float width) {
        in.metrics = &kM; in.palette = &kPal;
        in.contentWidth = width; in.zoom = 1.0f; in.engine = &engine;
        ctx.in = &in; ctx.list = &list;
    }
};

}  // namespace

TEST(CodeTableLayout, CodeBlockCardAndText) {
    Node n;
    n.block = BlockKind::CodeBlock;
    n.contentOffset = 42;
    n.contentLength = 999;  // the golden must NOT reuse this for text
    n.raw = U"alpha\nbravo\r\n";  // the trailing CR/LF run is trimmed
    Rig rig(300.0f);
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutCodeBlock(rig.ctx, n, 3, &y);

    ASSERT_EQ(static_cast<int>(b.kind),
              static_cast<int>(BlockKind::CodeBlock));
    ASSERT_EQ(b.lines.size(), static_cast<size_t>(2));  // split at the byte
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(3));
    EXPECT_EQ(rig.list.items[0].index(), kCard);  // the card first,
    EXPECT_EQ(rig.list.items[1].index(), kText);  // then a glyph line
    EXPECT_EQ(rig.list.items[2].index(), kText);  // per source line

    const float em = kM.codeFontSize * kPt;  // the code face in DIP
    const float h = kM.codeLineHeight * em;
    const display::FillRoundedRect& card =
        At<display::FillRoundedRect>(rig.list, 0);
    NEAR(card.rect.x, 0.0f); NEAR(card.rect.y, 0.0f);
    NEAR(card.rect.width, 300.0f);
    NEAR(card.rect.height, 2.0f * h + 2.0f * kM.codePad);
    NEAR(card.radius, kM.codeRadius);
    NEAR(card.color.r, kPal.codeBg.r);
    // The glyphs one code pad in, their source bytes from the offset.
    // The break byte stays on the line it ends, the account's rule.
    const Text& first = At<Text>(rig.list, 1);
    NEAR(first.x, kM.codePad); NEAR(first.top, kM.codePad);
    NEAR(first.line.width, 48.0f);  // "alpha\n" is 6 glyphs at 8 DIP
    NEAR(first.line.height, h);
    NEAR(first.line.ascent, (h - em * 1.5f) * 0.5f + em);
    EXPECT_EQ(first.line.runs[0].text, std::string("alpha\n"));
    EXPECT_EQ(first.line.runs[0].font.family, std::string("Cascadia Mono"));
    EXPECT_TRUE(first.line.runs[0].font.monospace);
    EXPECT_EQ(first.srcOffset, static_cast<uint32_t>(42));
    EXPECT_EQ(first.srcLength, static_cast<uint32_t>(6));
    const Text& second = At<Text>(rig.list, 2);
    NEAR(second.x, kM.codePad); NEAR(second.top, kM.codePad + h);
    NEAR(second.line.width, 40.0f);  // "bravo" is 5 glyph at 8 DIP
    EXPECT_EQ(second.line.runs[0].text, std::string("bravo"));
    EXPECT_EQ(second.srcOffset, static_cast<uint32_t>(48));
    EXPECT_EQ(second.srcLength, static_cast<uint32_t>(5));
    NEAR(b.lines[0].width, 48.0f);
    NEAR(b.lines[1].width, 40.0f);
    // The record: the card's height and the trimmed source range.
    NEAR(b.height, 2.0f * h + 2.0f * kM.codePad);
    EXPECT_EQ(b.srcOffset, static_cast<uint32_t>(42));
    EXPECT_EQ(b.srcLength, static_cast<uint32_t>(11));
    NEAR(y, b.height);
}

TEST(CodeTableLayout, TableSingleMeasureAndPaintOrder) {
    Node n;
    n.block = BlockKind::Table;
    n.contentOffset = 7;
    n.contentLength = 60;
    n.aligns = {ColumnAlign::Default, ColumnAlign::Right};
    TableRow head;  // the header row, both cells one line tall
    head.cells = {Cell(U"Name", true, 10, 14), Cell(U"Note", true, 15, 19)};
    TableRow body;
    TableCell alice = Cell(U"Alice", false, 30, 35);
    alice.inlineSpans.push_back(CellInlineSpan{0, 5, true});  // strong
    TableCell longcell = Cell(nullptr, false, 36, 58);
    longcell.text.assign(11, U'a');  // eleven body glyphs and eleven
    longcell.text.append(11, U'b');  // code ones: the span wraps
    longcell.inlineSpans.push_back(CellInlineSpan{11, 22, 0, 0, 1});
    body.cells = {alice, longcell};
    n.rows = {head, body};
    Rig rig(400.0f);
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 1, &y);

    // One measurement feeds the record: equal columns, one header
    // line, a wrapped body cell two lines tall.
    ASSERT_TRUE(b.table != nullptr);
    const layout::LaidTable& t = *b.table;
    EXPECT_EQ(t.columnCount, static_cast<size_t>(2));
    NEAR(t.colW, 200.0f);
    ASSERT_EQ(t.rowHeights.size(), static_cast<size_t>(2));
    NEAR(t.rowHeights[1], 2.0f * kBodyLine + 2.0f * kM.cellPadY);
    NEAR(b.height, 96.0f);
    // The cell source ranges stay monotonic across the whole table.
    EXPECT_TRUE(t.cells[0][1].srcCellStart > t.cells[0][0].srcCellStart);
    EXPECT_TRUE(t.cells[1][0].srcCellStart >= t.cells[0][1].srcCellEnd);
    EXPECT_TRUE(t.cells[1][1].srcCellStart > t.cells[1][0].srcCellStart);
    EXPECT_EQ(t.cells[1][1].srcCellStart, static_cast<uint32_t>(36));
    EXPECT_EQ(t.cells[1][1].srcCellEnd, static_cast<uint32_t>(58));
    EXPECT_EQ(t.cells[1][1].lines.size(), static_cast<size_t>(2));
    NEAR(t.cells[1][0].y, 36.0f);
    NEAR(t.cells[1][1].height, 60.0f);
    // Paint order: the zebra wash and the hairline grid are the
    // table's whole replay now. A cell leaves no replay text and
    // no replay clip: the record keeps the measured lines, and
    // each port paints them from that one record, so a cell
    // paints once, with one set of line breaks.
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(10));
    for (size_t i = 0; i < rig.list.items.size(); ++i)
        EXPECT_EQ(rig.list.items[i].index(), kFill);
    const display::FillRect& zebra = At<display::FillRect>(rig.list, 0);
    NEAR(zebra.rect.y, 36.0f);  // the body row only, never the header
    NEAR(zebra.color.r, kPal.tableRowAlt.r);
    for (size_t i = 1; i < 10; ++i)  // exactly nine hairlines
        NEAR(At<display::FillRect>(rig.list, i).color.r, kPal.tableBorder.r);
    NEAR(At<display::FillRect>(rig.list, 1).rect.y, 0.0f);   // the first top
    NEAR(At<display::FillRect>(rig.list, 6).rect.y, 95.0f);  // the second row
    const display::FillRect& vlast = At<display::FillRect>(rig.list, 9);
    NEAR(vlast.rect.x, 399.0f);  // right minus one, on the body row
    NEAR(vlast.rect.y, 36.0f);
    NEAR(vlast.rect.height, 60.0f);
    // The header cell: the record's line carries the semibold
    // face, its own source range on the cell.
    ASSERT_EQ(t.cells[0][0].lines.size(), static_cast<size_t>(1));
    const auto& nameLine = t.cells[0][0].lines[0];
    EXPECT_EQ(nameLine.runs[0].text, std::string("Name"));
    EXPECT_EQ(nameLine.runs[0].font.family,
              std::string("Segoe UI Semibold"));
    EXPECT_EQ(t.cells[0][0].srcCellStart, static_cast<uint32_t>(10));
    // The wrapped body cell: two lines, the body glyphs first,
    // then the code ones on their monospace face.
    const auto& wrapped = t.cells[1][1].lines;
    ASSERT_EQ(wrapped.size(), static_cast<size_t>(2));
    EXPECT_EQ(wrapped[0].runs[0].text, std::string(11, 'a'));
    EXPECT_EQ(wrapped[1].runs[0].text, std::string(11, 'b'));
    EXPECT_EQ(wrapped[1].runs[0].font.family,
              std::string("Cascadia Mono"));
    EXPECT_TRUE(wrapped[1].runs[0].font.monospace);
}

TEST(CodeTableLayout, Fallbacks) {
    Node n;  // an empty header cell keeps its placeholder line, an
    n.block = BlockKind::Table;  // empty row the theme's body line
    TableRow row0;
    row0.cells = {Cell(nullptr, true, 10, 11)};
    n.rows = {row0, TableRow()};  // zero cells: the fallback row
    Rig rig(400.0f);
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 0, &y);
    ASSERT_TRUE(b.table != nullptr);
    const layout::LaidTable& t = *b.table;
    ASSERT_EQ(t.rowHeights.size(), static_cast<size_t>(2));
    NEAR(t.rowHeights[1], kBodyLine + 2.0f * kM.cellPadY);  // no text
    NEAR(b.height, 72.0f);
    // The zebra and seven hairlines, nothing else: the
    // placeholder cell's line lives in the record now, where the
    // ports read it, not in the replay.
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(8));
    for (size_t i = 0; i < rig.list.items.size(); ++i)
        EXPECT_EQ(rig.list.items[i].index(), kFill);
    const auto& phLine = t.cells[0][0].lines[0];
    EXPECT_EQ(phLine.runs[0].text, std::string(" "));
    EXPECT_TRUE(phLine.runs[0].font.bold);
    EXPECT_EQ(t.cells[0][0].srcCellStart, static_cast<uint32_t>(10));
    display::DisplayList empty;  // no columns: no measure, no items
    rig.ctx.list = &empty;
    Node none;
    none.block = BlockKind::Table;
    none.rows = {TableRow()};
    float y2 = 5.0f;
    layout::LaidBlock b2 = layout::LayoutTableBlock(rig.ctx, none, 0, &y2);
    EXPECT_EQ(empty.items.size(), static_cast<size_t>(0));
    NEAR(y2, 5.0f);  // no columns, so the walk advances nothing
    EXPECT_EQ(b2.table->columnCount, static_cast<size_t>(0));
}


TEST(TableLayout, ADataTablePutsEveryCellInTheFixedFace) {
    // A CSV document asks for the code face in every cell, header and body
    // alike, so a column of figures is read down one edge whatever the
    // glyphs above it are.
    Node n;
    n.block = BlockKind::Table;
    n.tableMonospace = true;
    TableRow header;
    header.cells.push_back(Cell(U"name", true, 0, 4));
    header.cells.push_back(Cell(U"price", true, 5, 10));
    TableRow body;
    body.cells.push_back(Cell(U"Ole", false, 11, 14));
    body.cells.push_back(Cell(U"12.5", false, 15, 19));
    n.rows.push_back(header);
    n.rows.push_back(body);

    Rig rig(400.0f);
    float y = 40.0f;
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 0, &y);
    ASSERT_TRUE(b.table != nullptr);
    ASSERT_EQ(b.table->cells.size(), 2u);
    for (const std::vector<layout::LaidTableCell>& row : b.table->cells) {
        for (const layout::LaidTableCell& cell : row) {
            ASSERT_TRUE(!cell.lines.empty());
            for (const platform::TextLine& line : cell.lines) {
                for (const platform::GlyphRun& run : line.runs) {
                    // The spec carries the size in DIP, the metric in points.
                    NEAR(run.font.size, kM.codeFontSize * kPt);
                    EXPECT_TRUE(run.font.monospace);
                }
            }
        }
    }

    // The same table without the flag keeps the body face, which is what a
    // markdown table already draws.
    n.tableMonospace = false;
    Rig prose(400.0f);
    float y2 = 40.0f;
    layout::LaidBlock plain = layout::LayoutTableBlock(prose.ctx, n, 0, &y2);
    ASSERT_TRUE(plain.table != nullptr);
    ASSERT_TRUE(!plain.table->cells[0][0].lines.empty());
    const platform::GlyphRun& run = plain.table->cells[0][0].lines[0].runs[0];
    // A header cell is semibold at the body size, not a code run.
    NEAR(run.font.size, kM.bodyFontSize * kPt);
    EXPECT_FALSE(run.font.monospace);
}

TEST(TableLayout, ADataTablePutsAFilterButtonOnEveryHeaderColumn) {
    // The header of a table document carries one funnel per column, and the
    // button's hit area is the whole right gutter of that header cell.
    Node n;
    n.block = BlockKind::Table;
    n.tableMonospace = true;
    n.tableFilterActive = {0u, 1u};
    TableRow header;
    header.cells.push_back(Cell(U"name", true, 0, 4));
    header.cells.push_back(Cell(U"price", true, 5, 10));
    TableRow body;
    body.cells.push_back(Cell(U"Ole", false, 11, 14));
    body.cells.push_back(Cell(U"12.5", false, 15, 19));
    n.rows.push_back(header);
    n.rows.push_back(body);

    Rig rig(400.0f);
    float y = 40.0f;
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 0, &y);
    ASSERT_TRUE(b.table != nullptr);
    ASSERT_EQ(b.table->filterButtons.size(), 2u);
    const platform::Rect& first = b.table->filterButtons[0];
    const platform::Rect& second = b.table->filterButtons[1];
    // The target is wider than the gutter the text keeps clear: seven points
    // of ink is a hard thing to hit, so the button reaches further in.
    EXPECT_TRUE(first.width >= 24.0f);
    NEAR(first.width, kM.tableFilterHit);
    NEAR(first.height, b.table->rowHeights[0]);
    NEAR(first.y, 40.0f);
    // The second button sits on the second column's right edge.
    NEAR(second.x, b.table->colXs[1] + b.table->colWs[1] - kM.tableFilterHit);
    // Each mark has a centre, so a painter that redraws the band draws the
    // same funnel in the same place.
    ASSERT_EQ(b.table->filterCenters.size(), 2u);
    NEAR(b.table->filterCenters[0].y, 40.0f + b.table->rowHeights[0] * 0.5f);

    size_t column = 99;
    EXPECT_TRUE(layout::FilterButtonAt(b.table->filterButtons,
                                       first.x + first.width * 0.5f,
                                       first.y + 2.0f, &column));
    EXPECT_EQ(column, static_cast<size_t>(0));
    EXPECT_TRUE(layout::FilterButtonAt(b.table->filterButtons,
                                       second.x + second.width * 0.5f,
                                       second.y + 2.0f, &column));
    EXPECT_EQ(column, static_cast<size_t>(1));
    // The cell's text area is not the button, and neither is the body below.
    EXPECT_FALSE(layout::FilterButtonAt(b.table->filterButtons, 5.0f,
                                        first.y + 2.0f, &column));
    EXPECT_FALSE(layout::FilterButtonAt(b.table->filterButtons,
                                        first.x + 2.0f,
                                        first.y + first.height + 5.0f, &column));

    // One funnel per column, and the filtered column takes the link color.
    int funnels = 0;
    for (const display::Item& item : rig.list.items) {
        if (!std::holds_alternative<display::FillPolygon>(item)) continue;
        const display::FillPolygon& poly = std::get<display::FillPolygon>(item);
        EXPECT_EQ(poly.points.size(), static_cast<size_t>(6));
        ++funnels;
    }
    EXPECT_EQ(funnels, 2);

    // The header band: the row's own items, in document coordinates, so a
    // scrolling view can replay the band at its top edge. Nothing in it
    // reaches outside the row.
    EXPECT_TRUE(b.table->headerHeight > 0.0f);
    NEAR(b.table->headerHeight, b.table->rowHeights[0]);
    EXPECT_TRUE(b.table->header.items.size() >= 3u);
    for (const display::Item& item : b.table->header.items) {
        float top = 0.0f;
        float bottom = 0.0f;
        if (const auto* fill = std::get_if<display::FillRect>(&item)) {
            top = fill->rect.y;
            bottom = fill->rect.y + fill->rect.height;
        } else if (const auto* poly =
                       std::get_if<display::FillPolygon>(&item)) {
            for (const platform::Point& point : poly->points) {
                top = top == 0.0f ? point.y : (std::min)(top, point.y);
                bottom = (std::max)(bottom, point.y);
            }
        } else {
            continue;
        }
        EXPECT_TRUE(top >= 40.0f - 0.5f);
        EXPECT_TRUE(bottom <= 40.0f + b.table->headerHeight + 0.5f);
    }

    // The pin rule: a table that crosses the viewport's top edge keeps its
    // header there; one entirely below or above it pins nothing.
    float pinTop = -1.0f;
    EXPECT_TRUE(layout::PinnedHeader(b.y, b.height, b.table->headerHeight,
                                     40.0f + 10.0f, &pinTop));
    NEAR(pinTop, 50.0f);
    EXPECT_FALSE(layout::PinnedHeader(b.y, b.height, b.table->headerHeight,
                                      0.0f, &pinTop));
    EXPECT_FALSE(layout::PinnedHeader(b.y, b.height, b.table->headerHeight,
                                      b.y + b.height + 1.0f, &pinTop));
    // A table with no header row pins nothing, whatever the scroll.
    EXPECT_FALSE(layout::PinnedHeader(0.0f, 200.0f, 0.0f, 50.0f, &pinTop));

    // A prose table asks for no buttons: no gutter, no funnels.
    Node prose;
    prose.block = BlockKind::Table;
    prose.rows = n.rows;
    Rig plain(400.0f);
    float y2 = 40.0f;
    layout::LaidBlock p = layout::LayoutTableBlock(plain.ctx, prose, 0, &y2);
    ASSERT_TRUE(p.table != nullptr);
    EXPECT_TRUE(p.table->filterButtons.empty());
    int prose_funnels = 0;
    for (const display::Item& item : plain.list.items) {
        if (std::holds_alternative<display::FillPolygon>(item)) ++prose_funnels;
    }
    EXPECT_EQ(prose_funnels, 0);
}

TEST(TableLayout, DistributeTableColumnsMath) {
    // Words that fit the equal share keep the equal split.
    {
        const std::vector<float> min = {40.0f, 50.0f, 30.0f};
        const std::vector<float> w =
            layout::DistributeTableColumns(min, 300.0f, 3, 6.0f);
        EXPECT_EQ(w.size(), static_cast<size_t>(3));
        NEAR(w[0], 100.0f);
        NEAR(w[1], 100.0f);
        NEAR(w[2], 100.0f);
    }
    // A word wider than its equal share: its column takes the
    // demand, the others share what is left and never drop below
    // their own demand.
    {
        const std::vector<float> min = {291.0f, 57.0f, 276.0f, 84.0f};
        const std::vector<float> w =
            layout::DistributeTableColumns(min, 1100.0f, 4, 7.5f);
        EXPECT_EQ(w.size(), static_cast<size_t>(4));
        NEAR(w[0], 306.0f);   // 291 + 2 * 7.5
        NEAR(w[2], 291.0f);   // 276 + 2 * 7.5
        const float rest = 1100.0f - 306.0f - 291.0f;
        NEAR(w[1], rest * 0.5f);
        NEAR(w[3], rest * 0.5f);
        NEAR(w[0] + w[1] + w[2] + w[3], 1100.0f);
    }
    // Demands that outweigh the frame: the deficit is shared pro
    // rata, no column negative, no column eating the frame alone.
    {
        const std::vector<float> min = {600.0f, 700.0f, 900.0f, 500.0f};
        const std::vector<float> w =
            layout::DistributeTableColumns(min, 1100.0f, 4, 6.0f);
        EXPECT_EQ(w.size(), static_cast<size_t>(4));
        float sum = 0.0f;
        for (float x : w) {
            EXPECT_GT(x, 0.0f);
            sum += x;
        }
        NEAR(sum, 1100.0f);
        EXPECT_GT(w[2], w[0]);  // the widest demand keeps the widest
        EXPECT_GT(w[1], w[0]);  // share, pro rata
        EXPECT_LT(w[2], 906.0f);  // and never its whole demand
    }
    // No columns at all: nothing to divide.
    {
        const std::vector<float> empty;
        const std::vector<float> w =
            layout::DistributeTableColumns(empty, 500.0f, 0, 6.0f);
        EXPECT_EQ(w.size(), static_cast<size_t>(0));
    }
}

// Every grid and wash fill lies inside its own table's block. The
// fills once started at y 0 of the document, so a table laid below
// the heading painted its grid over the heading and paragraphs above.
TEST(TableLayout, FillsSitOnTheTablesOwnBlock) {
    Node n;
    n.block = BlockKind::Table;
    TableRow head, body;
    head.cells = {Cell(U"Name", true, 0, 4), Cell(U"Value", true, 5, 10)};
    body.cells = {Cell(U"alpha", false, 11, 16), Cell(U"bravo", false, 17, 22)};
    n.rows = {head, body, body};
    Rig rig(400.0f);
    float y = 40.0f;  // a heading and a paragraph sit above the table
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 1, &y);
    ASSERT_TRUE(b.height > 0.0f);
    NEAR(b.y, 40.0f);
    size_t fills = 0;
    for (const display::Item& item : rig.list.items) {
        if (!std::holds_alternative<display::FillRect>(item)) continue;
        const platform::Rect& r = std::get<display::FillRect>(item).rect;
        ++fills;
        EXPECT_TRUE(r.y >= b.y - 0.01f);
        EXPECT_TRUE(r.y + r.height <= b.y + b.height + 0.01f);
    }
    EXPECT_TRUE(fills > 0);
}

// A markdown table's header row is the page's own paper in the GitHub style,
// so the band a scrolling view pins has to carry a surface of its own: without
// one the rows scroll through the column names. The band's first item is that
// surface, opaque and exactly as tall as the row.
TEST(TableLayout, AProseTablePinsItsHeaderWithASurfaceOfItsOwn) {
    Node n;
    n.block = BlockKind::Table;
    TableRow header;
    header.cells.push_back(Cell(U"Kolonne 1", true, 0, 9));
    header.cells.push_back(Cell(U"Kolonne 2", true, 10, 19));
    TableRow body;
    body.cells.push_back(Cell(U"a", false, 20, 21));
    body.cells.push_back(Cell(U"b", false, 22, 23));
    n.rows.push_back(header);
    n.rows.push_back(body);

    Rig rig(400.0f);
    float y = 40.0f;
    layout::LaidBlock b = layout::LayoutTableBlock(rig.ctx, n, 0, &y);
    ASSERT_TRUE(b.table != nullptr);
    EXPECT_TRUE(b.table->headerHeight > 0.0f);
    ASSERT_TRUE(!b.table->header.items.empty());
    const auto* surface =
        std::get_if<display::FillRect>(&b.table->header.items[0]);
    ASSERT_TRUE(surface != nullptr);
    NEAR(surface->rect.y, 40.0f);
    NEAR(surface->rect.height, b.table->headerHeight);
    EXPECT_TRUE(surface->color.a > 0.99f);  // the body goes under it

    // The surface is the header's own fill when the style paints one, and the
    // muted row fill otherwise, so a pinned header still reads as a header.
    if (!kM.tableHeaderFill) {
        const platform::Color alt{kPal.tableRowAlt.r, kPal.tableRowAlt.g,
                                    kPal.tableRowAlt.b, kPal.tableRowAlt.a};
        EXPECT_NEAR(surface->color.r, alt.r, 0.001f);
        EXPECT_NEAR(surface->color.g, alt.g, 0.001f);
        EXPECT_NEAR(surface->color.b, alt.b, 0.001f);
    }

    // The band carries its own words: the copy a view replays at its edge is
    // all a pinned header needs, so neither port has to paint the row a
    // second time at a place it never measured (that second paint is what
    // left the band blank on the mac).
    std::string bandText;
    bool sawText = false;
    for (const display::Item& item2 : b.table->header.items) {
        if (const auto* text = std::get_if<display::Text>(&item2)) {
            sawText = true;
            for (const platform::GlyphRun& run : text->line.runs) {
                bandText += run.text;
            }
        }
    }
    EXPECT_TRUE(sawText);
    EXPECT_TRUE(bandText.find("Kolonne 1") != std::string::npos);
    EXPECT_TRUE(bandText.find("Kolonne 2") != std::string::npos);

    // And the row pins as soon as the table crosses the viewport's top edge.
    float pinTop = 0.0f;
    EXPECT_TRUE(layout::PinnedHeader(b.y, b.height, b.table->headerHeight,
                                     b.y + 1.0f, &pinTop));
    NEAR(pinTop, b.y + 1.0f);
}
