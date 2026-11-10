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
