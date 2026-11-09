// The golden tests of the shared layout, plan Batch 3.2. The fake
// engine measures every glyph at 8 DIP and the theme's own metrics
// feed the walk, so these numbers are the same on both runners or the
// layout has drifted, whatever the platform prints under it.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "blockspacing.h"
#include "layout/document_layout.h"

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
