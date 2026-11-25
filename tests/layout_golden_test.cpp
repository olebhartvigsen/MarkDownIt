// The golden tests of the shared layout, plan Batch 3.2. The fake
// engine measures every glyph at 8 DIP and the theme's own metrics
// feed the walk, so these numbers are the same on both runners or the
// layout has drifted, whatever the platform prints under it.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "blockspacing.h"
#include "layout/document_layout.h"
#include "../src/parser.h"

#include <variant>

TEST(LayoutGolden, BreakAndHeadingList) {
    Document doc;
    {
        Node n;
        n.block = BlockKind::Heading;
        n.level = 2;
        n.contentOffset = 5; n.contentLength = 8;  // "Overview"
        InlineBlock child;
        child.text = U"Overview";
        n.children.push_back(child);
        doc.nodes.push_back(n);
    }
    {
        Node n;
        n.block = BlockKind::ThematicBreak;
        n.contentOffset = 20; n.contentLength = 3;  // "---"
        doc.nodes.push_back(n);
    }
    {
        Node n;
        n.block = BlockKind::Heading;
        n.level = 6;
        n.contentOffset = 30; n.contentLength = 9;
        InlineBlock child;
        child.text = U"Deep dive";
        n.children.push_back(child);
        doc.nodes.push_back(n);
    }

    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = 600.0f; in.zoom = 1.0f; in.engine = &engine;
    layout::DocumentLayout laid = layout::LayoutDocument(in);

    // Two headings and a break, in order, one text item each and the
    // level 2 heading a rule besides.
    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(3));
    ASSERT_EQ(laid.list.items.size(), static_cast<size_t>(4));
    EXPECT_TRUE(std::holds_alternative<display::Text>(
        laid.list.items[0]));
    EXPECT_TRUE(std::holds_alternative<display::FillRect>(
        laid.list.items[1]));
    EXPECT_TRUE(std::holds_alternative<display::FillRect>(
        laid.list.items[2]));
    EXPECT_TRUE(std::holds_alternative<display::Text>(
        laid.list.items[3]));

    const display::Text& head2 =
        std::get<display::Text>(laid.list.items[0]);
    EXPECT_EQ(head2.srcOffset, static_cast<uint32_t>(5));
    EXPECT_EQ(head2.srcLength, static_cast<uint32_t>(8));
    // One run, the theme's semibold heading face, the heading alignment.
    ASSERT_EQ(head2.line.runs.size(), static_cast<size_t>(1));
    EXPECT_EQ(head2.line.runs[0].font.family,
              std::string("Segoe UI Semibold"));
    ASSERT_EQ(head2.line.runs[0].text, std::string("Overview"));
    // The fake engine: 8 DIP per glyph, so 8 glyphs are 64 wide.
    EXPECT_NEAR(head2.line.width, 64.0f, 0.001f);
    // The CSS line box, the theme's own arithmetic.
    const float em = m.headingSizes[2] * (96.0f / 72.0f);
    EXPECT_NEAR(head2.line.height, m.headingLineHeight * em, 0.001f);

    // The hairline under the H2: .3em of pad below the text.
    const display::FillRect& rule =
        std::get<display::FillRect>(laid.list.items[1]);
    const float pad = m.headingRulePadRatio * em;
    EXPECT_NEAR(rule.rect.y, head2.top + head2.line.height + pad, 0.001f);
    EXPECT_NEAR(rule.rect.height, m.headingRuleThickness, 0.001f);
    EXPECT_NEAR(rule.rect.width, 600.0f, 0.001f);

    // The break: a gap after the heading, the .25em bar, full width.
    const display::FillRect& hr =
        std::get<display::FillRect>(laid.list.items[2]);
    const float gap = GapForTransition(BlockKind::Heading,
                                       BlockKind::ThematicBreak, m);
    EXPECT_NEAR(hr.rect.y,
                head2.top + head2.line.height + pad +
                    rule.rect.height + gap,
                0.001f);
    EXPECT_NEAR(hr.rect.height, 4.0f, 0.001f);
    EXPECT_NEAR(hr.rect.width, 600.0f, 0.001f);

    // The level 6 heading reads muted and carries no rule.
    const display::Text& head6 =
        std::get<display::Text>(laid.list.items[3]);
    EXPECT_NEAR(head6.line.runs[0].r, pal.quoteText.r, 0.001f);

    // The document column, no platform rounding anywhere.
    EXPECT_NEAR(laid.list.width, 600.0f, 0.001f);
}

// An empty H1 directly above another H1, the case a reader reported as a
// line the caret could not be put on: the block must keep one blank line of
// its own, with its own source position, so the walk reserves the band and
// the caret and the hit test have something to land on.
TEST(LayoutGolden, EmptyHeadingKeepsItsLine) {
    Document doc;
    ParseMarkdown("#\n# Dette\n", doc);
    ASSERT_EQ(doc.nodes.size(), static_cast<size_t>(2));
    ASSERT_EQ(doc.nodes[0].block, BlockKind::Heading);
    EXPECT_TRUE(doc.nodes[0].children.empty());

    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = 600.0f; in.zoom = 1.0f; in.engine = &engine;
    in.editing = true;
    layout::DocumentLayout laid = layout::LayoutDocument(in);

    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(2));
    const layout::LaidBlock& empty = laid.blocks[0];
    const layout::LaidBlock& titled = laid.blocks[1];
    // One line, the same band a real H1 keeps, not a zero height block.
    ASSERT_EQ(empty.lines.size(), static_cast<size_t>(1));
    ASSERT_EQ(titled.lines.size(), static_cast<size_t>(1));
    const float lineH = titled.lines[0].height;
    EXPECT_GT(empty.height, 0.0f);
    // The same band a real H1 keeps: one line plus the style's hairline.
    EXPECT_NEAR(empty.height, titled.height, 0.001f);

    // The blank run maps to the caret position the parser gave the heading:
    // the end of its marker, no visible characters.
    ASSERT_GE(laid.list.items.size(), static_cast<size_t>(2));
    const display::Text& blank = std::get<display::Text>(laid.list.items[0]);
    EXPECT_EQ(blank.srcOffset, doc.nodes[0].contentOffset);
    EXPECT_EQ(blank.srcLength, static_cast<uint32_t>(0));
    EXPECT_NEAR(blank.line.height, lineH, 0.001f);
    std::string drawn;
    for (const platform::GlyphRun& run : blank.line.runs) drawn += run.text;
    EXPECT_EQ(drawn, std::string(" "));

    // The style's hairline under an H1, the way a browser draws an empty h1.
    EXPECT_TRUE(std::holds_alternative<display::FillRect>(laid.list.items[1]));

    // The real heading below it starts after the empty block's band.
    EXPECT_GE(laid.blocks[1].y, empty.y + empty.height - 0.001f);
    EXPECT_NEAR(laid.list.height, laid.blocks[1].y + laid.blocks[1].height,
                0.001f);
}

// A data table wider than the page column widens the document's own width,
// which is the number both ports size their horizontal scroll range from. A
// 33 column localisation export used to be squeezed into the window instead,
// with prose columns a character and a half wide.
TEST(LayoutGolden, AWideDataTableWidensTheDocument) {
    Document doc;
    Node table;
    table.block = BlockKind::Table;
    table.tableMonospace = true;
    TableRow head;
    TableRow body;
    for (int c = 0; c < 6; ++c) {
        TableCell header;
        header.isHeader = true;
        header.text = U"fieldnamecolumn";
        head.cells.push_back(header);
        TableCell value;
        value.text = U"a value that needs a wider column than this";
        body.cells.push_back(value);
    }
    table.rows = {head, body};
    doc.nodes.push_back(table);

    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = 600.0f; in.zoom = 1.0f; in.engine = &engine;
    layout::DocumentLayout laid = layout::LayoutDocument(in);

    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(1));
    const layout::LaidBlock& block = laid.blocks[0];
    // The page column is 600; the table is its six columns wide.
    EXPECT_GT(block.width, 600.0f);
    EXPECT_NEAR(laid.list.width, block.x + block.width, 0.001f);
}

// Regression: a gantt fence between two headings must be laid out with
// real height, not skipped by the block layout's "which kind owns this
// node" switch. A gap in that switch made the diagram invisible even
// though the parser and the mermaid layout itself were both correct.
TEST(LayoutGolden, GanttFenceGetsOwnedAndSized) {
    std::string md =
        "# Before\n"
        "\n"
        "```mermaid\n"
        "gantt\n"
        "    dateFormat DD/MM/YYYY\n"
        "    section S\n"
        "    Task A :a1, 01/03/2026, 5d\n"
        "```\n"
        "\n"
        "# After\n";
    Document doc;
    ParseMarkdown(md, doc);
    ASSERT_EQ(doc.nodes.size(), static_cast<size_t>(3));
    ASSERT_EQ(doc.nodes[1].block, BlockKind::MermaidGantt);
    ASSERT_TRUE(doc.nodes[1].mermaid_gantt != nullptr);

    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = 600.0f; in.zoom = 1.0f; in.engine = &engine;
    layout::DocumentLayout laid = layout::LayoutDocument(in);

    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(3));
    // The gantt block (middle) must claim real height: a zero height
    // here is exactly the symptom the user saw (diagram not shown).
    EXPECT_GT(laid.blocks[1].height, 0.0f);
    EXPECT_EQ(laid.blocks[1].kind, BlockKind::MermaidGantt);
    // The following heading must sit below the gantt block's bottom,
    // not stacked straight on top of the first heading.
    EXPECT_GT(laid.blocks[2].y, laid.blocks[0].y + laid.blocks[0].height);
}
