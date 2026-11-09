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
using display::FillRect;
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
    ASSERT_EQ(b.lines.size(), static_cast<size_t>(1));  // one fake run
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rig.list.items[0].index(), kCard);  // the card first,
    EXPECT_EQ(rig.list.items[1].index(), kText);  // then the glyphs

    const float em = kM.codeFontSize * kPt;  // the code face in DIP
    const float h = kM.codeLineHeight * em;
    const display::FillRoundedRect& card =
        At<display::FillRoundedRect>(rig.list, 0);
    NEAR(card.rect.x, 0.0f); NEAR(card.rect.y, 0.0f);
    NEAR(card.rect.width, 300.0f);
    NEAR(card.rect.height, h + 2.0f * kM.codePad);
    NEAR(card.radius, kM.codeRadius);
    NEAR(card.color.r, kPal.codeBg.r);
    // The glyphs one code pad in, their source bytes from the offset.
    const Text& text = At<Text>(rig.list, 1);
    NEAR(text.x, kM.codePad); NEAR(text.top, kM.codePad);
    NEAR(text.line.width, 88.0f);  // 11 code points at 8 DIP
    NEAR(text.line.height, h);
    NEAR(text.line.ascent, (h - em * 1.5f) * 0.5f + em);
    EXPECT_EQ(text.line.runs[0].text, std::string("alpha\nbravo"));
    EXPECT_EQ(text.line.runs[0].font.family, std::string("Cascadia Mono"));
    EXPECT_TRUE(text.line.runs[0].font.monospace);
    EXPECT_EQ(text.srcOffset, static_cast<uint32_t>(42));
    EXPECT_EQ(text.srcLength, static_cast<uint32_t>(11));
    // The record: the card's height and the trimmed source range.
    NEAR(b.height, h + 2.0f * kM.codePad);
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
    // Paint order, by variant index: the zebra wash, the hairline
    // grid, one clip pair around each cell's text, the wrapped cell
    // keeping its two Text items inside one pair.
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(23));
    const size_t order[] = {kFill, kFill, kFill, kFill, kFill,
                            kFill, kFill, kFill, kFill, kFill,
                            kClip, kText, kPop, kClip, kText, kPop,
                            kClip, kText, kPop, kClip, kText, kText, kPop};
    for (size_t i = 0; i < rig.list.items.size(); ++i)
        EXPECT_EQ(rig.list.items[i].index(), order[i]);
    const FillRect& zebra = At<FillRect>(rig.list, 0);
    NEAR(zebra.rect.y, 36.0f);  // the body row only, never the header
    NEAR(zebra.color.r, kPal.tableRowAlt.r);
    for (size_t i = 1; i < 10; ++i)  // exactly nine hairlines
        NEAR(At<FillRect>(rig.list, i).color.r, kPal.tableBorder.r);
    NEAR(At<FillRect>(rig.list, 1).rect.y, 0.0f);   // the first top
    NEAR(At<FillRect>(rig.list, 6).rect.y, 95.0f);  // the second row
    const FillRect& vlast = At<FillRect>(rig.list, 9);
    NEAR(vlast.rect.x, 399.0f);  // right minus one, on the body row
    NEAR(vlast.rect.y, 36.0f);
    NEAR(vlast.rect.height, 60.0f);
    // The header cell: the semibold face at the pad, its own source
    // range, clipped to the padded inner rect of its row.
    const PushClip& clip0 = At<PushClip>(rig.list, 10);
    NEAR(clip0.rect.x, 13.0f); NEAR(clip0.rect.y, 6.0f);
    NEAR(clip0.rect.width, 174.0f);
    const Text& nameText = At<Text>(rig.list, 11);
    NEAR(nameText.x, 13.0f); NEAR(nameText.top, 6.0f);
    EXPECT_EQ(nameText.line.runs[0].font.family,
              std::string("Segoe UI Semibold"));
    EXPECT_EQ(nameText.srcOffset, static_cast<uint32_t>(10));
    EXPECT_EQ(nameText.srcLength, static_cast<uint32_t>(4));
    // The right aligned column shifts against the 174 DIP inner
    // width; the wrapped cell walks its source line by line.
    NEAR(At<Text>(rig.list, 14).x, 213.0f + (174.0f - 32.0f));
    const Text& w1 = At<Text>(rig.list, 20);
    const Text& w2 = At<Text>(rig.list, 21);
    NEAR(w1.x, 213.0f + (174.0f - 88.0f));
    NEAR(w2.x, w1.x);
    NEAR(w1.top, 42.0f); NEAR(w2.top, 66.0f);
    EXPECT_EQ(w2.line.runs[0].font.family, std::string("Cascadia Mono"));
    EXPECT_TRUE(w2.line.runs[0].font.monospace);
    EXPECT_EQ(w1.srcOffset, static_cast<uint32_t>(36));
    EXPECT_EQ(w1.srcLength, static_cast<uint32_t>(11));
    EXPECT_EQ(w2.srcOffset, static_cast<uint32_t>(47));
    EXPECT_EQ(w2.srcLength, static_cast<uint32_t>(11));
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
    // The zebra, seven hairlines, one placeholder cell.
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(11));
    const Text& ph = At<Text>(rig.list, 9);
    EXPECT_EQ(ph.line.runs[0].text, std::string(" "));
    EXPECT_TRUE(ph.line.runs[0].font.bold);
    EXPECT_EQ(ph.srcOffset, static_cast<uint32_t>(10));
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
