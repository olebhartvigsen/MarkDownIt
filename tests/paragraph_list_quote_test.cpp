// The golden tests of the shared paragraph, list and quote layout,
// plan Batch 3.2 steps 2 to 4. The fake engine draws every code point
// at 8 DIP and the theme's own metrics feed the walk, so these
// numbers hold on both runners or the layout has drifted.

#include "blockspacing.h"
#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "layout/paragraph_layout.h"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace {

const float kEm = 12.0f * 96.0f / 72.0f;  // the body size in DIP
const float kLine = 1.5f * kEm;           // the body line box
// The fixed gutter (20 * 0.6 + 4 = 16) is narrower than the bullet
// marker itself: "\xE2\x80\xA2 " is 2 code points * 8 DIP (16) plus
// the gutter (4), so the measured width (20) wins here. A task item
// has no marker text to measure (the checkbox is its marker), so its
// gutter stays the fixed formula: kTaskMarkerW, not kMarkerW.
const float kMarkerW = 20.0f;
const float kTaskMarkerW = 20.0f * 0.6f + 4.0f;

// The packaged node builder the tests may keep: one text block with
// its depth, order flag and task state.
Node TextNodeOf(BlockKind kind, const std::u32string& text, int depth = 0,
                bool ordered = false, int task = -1) {
    Node n;
    n.block = kind;
    n.depth = depth;
    n.ordered = ordered;
    n.taskMark = task;
    if (!text.empty()) {
        InlineBlock child;
        child.kind = InlineKind::Text;
        child.text = text;
        n.children.push_back(child);
    }
    return n;
}

InlineBlock ImageOf(const char* url) {
    InlineBlock ib;
    ib.kind = InlineKind::Image;
    ib.url = url;
    return ib;
}

// The run every golden makes: the fake engine, the theme's own
// metrics, one column width.
layout::DocumentLayout RunLayout(const Document& doc, float width,
                                 fake::FakeTextEngine* engine,
                                 bool editing = false) {
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = width; in.zoom = 1.0f; in.engine = engine;
    in.editing = editing;
    return layout::LayoutDocument(in);
}

}  // namespace

TEST(ParagraphListQuote, WrappedParagraphAndReadingStrip) {
    Document doc;
    doc.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U"one "));
    InlineBlock second, third;
    second.text = U"two ";
    third.text = U"three";
    doc.nodes[0].children.push_back(second);
    doc.nodes[0].children.push_back(third);
    doc.nodes[0].contentOffset = 10;
    doc.nodes[0].contentLength = 13;

    fake::FakeTextEngine engine;
    layout::DocumentLayout laid = RunLayout(doc, 64.0f, &engine);

    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(1));
    // The fake breaks between runs: 32 + 32 fits, 40 does not.
    ASSERT_EQ(laid.list.items.size(), static_cast<size_t>(2));
    std::string all;
    for (const display::Item& item : laid.list.items) {
        const display::Text& text = std::get<display::Text>(item);
        for (const platform::GlyphRun& run : text.line.runs) all += run.text;
        EXPECT_NEAR(text.line.height, kLine, 0.001f);
        EXPECT_EQ(text.srcOffset, static_cast<uint32_t>(10));
        EXPECT_EQ(text.srcLength, static_cast<uint32_t>(13));
    }
    EXPECT_EQ(all, std::string("one two three"));
    const display::Text& secondItem =
        std::get<display::Text>(laid.list.items[1]);
    EXPECT_NEAR(secondItem.top, kLine, 0.001f);
    EXPECT_NEAR(secondItem.line.width, 40.0f, 0.001f);
    EXPECT_NEAR(laid.blocks[0].height, 2.0f * kLine, 0.001f);
    EXPECT_NEAR(laid.list.height, 2.0f * kLine, 0.001f);

    // A virtual empty paragraph keeps a line for the caret while
    // editing, and collapses to the reading strip when not edited.
    Document blank;
    blank.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U""));
    blank.nodes[0].virtualEmptyParagraph = true;
    layout::DocumentLayout editingRun = RunLayout(blank, 300.0f, &engine, true);
    const display::Text& line =
        std::get<display::Text>(editingRun.list.items[0]);
    EXPECT_EQ(line.line.runs.size() >= 1u, true);
    EXPECT_EQ(line.line.runs[0].text, std::string(" "));
    EXPECT_NEAR(line.line.height, kLine, 0.001f);
    layout::DocumentLayout reading = RunLayout(blank, 300.0f, &engine);
    const display::Text& strip =
        std::get<display::Text>(reading.list.items[0]);
    EXPECT_NEAR(strip.line.height, layout::kReadBlankLineStripDip, 0.001f);
    EXPECT_NEAR(reading.list.height, layout::kReadBlankLineStripDip, 0.001f);
}

TEST(ParagraphListQuote, ListMarkersTasksAndOrderedCounter) {
    Document doc;
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"alpha", 0));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"beta", 1));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"gamma", 2));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"delta", 3));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"done", 0, false, 1));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"", 0, false, 0));
    doc.nodes[0].contentOffset = 100;
    doc.nodes[0].contentLength = 5;

    fake::FakeTextEngine engine;
    const Palette pal = BasePalette();
    layout::DocumentLayout laid = RunLayout(doc, 300.0f, &engine);

    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(6));
    // The shared glyph table cycles with the depth; a task item has
    // no text marker of its own.
    EXPECT_EQ(laid.blocks[0].markerText, ListMarkerUtf8(0));
    EXPECT_EQ(laid.blocks[1].markerText, ListMarkerUtf8(1));
    EXPECT_EQ(laid.blocks[2].markerText, ListMarkerUtf8(2));
    EXPECT_EQ(laid.blocks[3].markerText, ListMarkerUtf8(0));
    EXPECT_EQ(laid.blocks[4].markerText, std::string());
    EXPECT_EQ(laid.blocks[5].markerText, std::string());
    EXPECT_TRUE(laid.blocks[4].task);
    EXPECT_TRUE(laid.blocks[5].task);
    EXPECT_FALSE(laid.blocks[0].task);
    // The indent steps with the depth and every item keeps one line
    // box plus the 4 DIP item gap.
    EXPECT_NEAR(laid.blocks[0].x, 20.0f, 0.001f);
    EXPECT_NEAR(laid.blocks[1].x, 40.0f, 0.001f);
    EXPECT_NEAR(laid.blocks[3].x, 80.0f, 0.001f);
    for (size_t i = 0; i < 6; ++i)
        EXPECT_NEAR(laid.blocks[i].y, 28.0f * i, 0.001f);
    // Four clipped markers plus their lines, two task boxes, one
    // check of two segments: the whole list in one item order.
    ASSERT_EQ(laid.list.items.size(), static_cast<size_t>(22));

    // The first marker: the shared bullet glyph, clipped to the
    // gutter, borrowing the first line's box, owned by no source byte.
    const display::PushClip& clip =
        std::get<display::PushClip>(laid.list.items[0]);
    EXPECT_NEAR(clip.rect.x, 20.0f, 0.001f);
    EXPECT_NEAR(clip.rect.width, kMarkerW, 0.001f);
    const display::Text& marker =
        std::get<display::Text>(laid.list.items[1]);
    EXPECT_EQ(marker.line.runs.size() >= 1u, true);
    EXPECT_EQ(marker.line.runs[0].text, std::string("\xE2\x80\xA2 "));
    EXPECT_EQ(marker.srcOffset, static_cast<uint32_t>(100));
    EXPECT_EQ(marker.srcLength, static_cast<uint32_t>(0));

    // The checkbox of the done item: thirteen DIP, centred on the
    // first line, and the item text past the gutter.
    const display::StrokeRect& box =
        std::get<display::StrokeRect>(laid.list.items[16]);
    EXPECT_NEAR(box.rect.x, 21.0f, 0.001f);
    EXPECT_NEAR(box.rect.y, 112.0f + (kLine - 13.0f) * 0.5f, 0.001f);
    EXPECT_NEAR(box.rect.width, 13.0f, 0.001f);
    EXPECT_NEAR(box.color.r, pal.textPrimary.r, 0.001f);
    EXPECT_NEAR(box.lineWidth, 1.2f, 0.001f);
    const display::Text& itemText =
        std::get<display::Text>(laid.list.items[19]);
    EXPECT_NEAR(itemText.x, 20.0f + kTaskMarkerW, 0.001f);
    EXPECT_NEAR(itemText.top, 112.0f, 0.001f);
    // The textless item keeps its addressable space run.
    EXPECT_EQ(laid.blocks[5].lines.size() >= 1u, true);
    EXPECT_EQ(laid.blocks[5].lines[0].runs[0].text, std::string(" "));

    // The ordered counter resets on kind and depth, survives an
    // unordered item and counts a task item without drawing it.
    Document ordered;
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"a", 0, true));
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"b", 0, false));
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"c", 0, true));
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"d", 0, true, 1));
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"e", 1, true));
    ordered.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U"x"));
    ordered.nodes.push_back(TextNodeOf(BlockKind::List, U"f", 0, true));
    layout::DocumentLayout counter = RunLayout(ordered, 300.0f, &engine);
    ASSERT_EQ(counter.blocks.size(), static_cast<size_t>(7));
    EXPECT_EQ(counter.blocks[0].markerText, std::string("1. "));
    EXPECT_EQ(counter.blocks[1].markerText, ListMarkerUtf8(0));
    EXPECT_EQ(counter.blocks[2].markerText, std::string("2. "));
    EXPECT_EQ(counter.blocks[3].markerText, std::string());
    EXPECT_EQ(counter.blocks[4].markerText, std::string("1. "));
    EXPECT_EQ(counter.blocks[6].markerText, std::string("1. "));
}

TEST(ParagraphListQuote, QuoteBarAndIndent) {
    Document doc;
    doc.nodes.push_back(TextNodeOf(BlockKind::BlockQuote, U"quoted"));
    doc.nodes.push_back(TextNodeOf(BlockKind::BlockQuote, U"deep", 2));

    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::DocumentLayout laid = RunLayout(doc, 200.0f, &engine);

    // The bar first, then the text, one item per line.
    ASSERT_EQ(laid.list.items.size(), static_cast<size_t>(4));
    const display::FillRect& bar =
        std::get<display::FillRect>(laid.list.items[0]);
    EXPECT_NEAR(bar.rect.x, (20.0f - m.quoteIndent + 4.0f) -
                                m.quoteBarWidth * 0.5f, 0.001f);
    EXPECT_NEAR(bar.rect.y, 0.0f, 0.001f);
    EXPECT_NEAR(bar.rect.width, m.quoteBarWidth, 0.001f);
    EXPECT_NEAR(bar.rect.height, kLine, 0.001f);
    EXPECT_NEAR(bar.color.g, pal.quoteBar.g, 0.001f);
    const display::Text& text = std::get<display::Text>(laid.list.items[1]);
    EXPECT_NEAR(text.x, 20.0f, 0.001f);
    EXPECT_NEAR(text.line.runs[0].g, pal.quoteText.g, 0.001f);
    // The nesting chain: three indents in, and the second bar pushed
    // past the first quote by the paragraph gap.
    EXPECT_NEAR(laid.blocks[1].x, 60.0f, 0.001f);
    const display::FillRect& deep =
        std::get<display::FillRect>(laid.list.items[2]);
    EXPECT_NEAR(deep.rect.x, 60.0f - m.quoteIndent + 4.0f -
                                 m.quoteBarWidth * 0.5f, 0.001f);
    EXPECT_NEAR(deep.rect.y, kLine + m.paraGap, 0.001f);
    EXPECT_NEAR(laid.list.height, 2.0f * kLine + m.paraGap, 0.001f);
}

TEST(ParagraphListQuote, ImageChargeAboveText) {
    Document doc;
    Node n = TextNodeOf(BlockKind::Paragraph, U"caption");
    n.children.insert(n.children.begin(), ImageOf("wide.png"));
    n.children.insert(n.children.begin(), ImageOf("remote.png"));
    n.children.insert(n.children.begin(), ImageOf("small.png"));
    doc.nodes.push_back(n);

    fake::FakeTextEngine engine;
    layout::LayoutInput in;
    in.doc = &doc;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    in.metrics = &m; in.palette = &pal;
    in.contentWidth = 300.0f; in.zoom = 1.0f; in.engine = &engine;
    in.imageSize = [](const std::string& url) -> layout::ImageSize {
        layout::ImageSize size;
        if (url == "small.png") { size.width = 200; size.height = 100; }
        if (url == "wide.png")  { size.width = 600; size.height = 300; }
        size.known = size.width > 0.0f;
        return size;  // remote.png stays unknown
    };
    layout::DocumentLayout laid = layout::LayoutDocument(in);

    // Three images stacked above the text, each fitted by the browser
    // rule, each charging one paragraph gap of room.
    ASSERT_EQ(laid.blocks.size(), static_cast<size_t>(1));
    ASSERT_EQ(laid.list.items.size(), static_cast<size_t>(4));
    const display::Image& smallItem =
        std::get<display::Image>(laid.list.items[0]);
    EXPECT_EQ(smallItem.url, std::string("small.png"));
    EXPECT_TRUE(smallItem.kind == platform::ImageKind::Raster);
    EXPECT_NEAR(smallItem.rect.width, 200.0f, 0.001f);
    EXPECT_NEAR(smallItem.rect.height, 100.0f, 0.001f);
    // An unknown size charges the placeholder: full width, 100 DIP.
    const display::Image& remoteItem =
        std::get<display::Image>(laid.list.items[1]);
    EXPECT_NEAR(remoteItem.rect.y, 116.0f, 0.001f);
    EXPECT_NEAR(remoteItem.rect.width, 300.0f, 0.001f);
    EXPECT_NEAR(remoteItem.rect.height, 100.0f, 0.001f);
    // A wide image shrinks to the column and keeps its aspect.
    const display::Image& wideItem =
        std::get<display::Image>(laid.list.items[2]);
    EXPECT_NEAR(wideItem.rect.y, 232.0f, 0.001f);
    EXPECT_NEAR(wideItem.rect.width, 300.0f, 0.001f);
    EXPECT_NEAR(wideItem.rect.height, 150.0f, 0.001f);
    EXPECT_NEAR(wideItem.naturalWidth, 600.0f, 0.001f);
    // The text starts below every image, one line of it.
    const display::Text& text = std::get<display::Text>(laid.list.items[3]);
    EXPECT_NEAR(text.top, 398.0f, 0.001f);
    EXPECT_EQ(laid.blocks[0].imageUrls.size(), static_cast<size_t>(3));
    EXPECT_NEAR(laid.blocks[0].imagesHeight, 398.0f, 0.001f);
    EXPECT_NEAR(laid.blocks[0].height, 398.0f + kLine, 0.001f);
}

// The walk brackets the engine once per pass. A block that bracketed it
// again dropped every line measured before it, and an engine with no
// retained line answers a hit test with the start of the line: a click
// mid-line put the caret at the line's beginning, and the caret could
// not be drawn inside the line at all. The heading did exactly that.
TEST(ParagraphListQuote, LayoutBracketsTheEngineOncePerPass) {
    Document doc;
    doc.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U"before"));
    Node heading = TextNodeOf(BlockKind::Heading, U"title");
    heading.level = 2;
    doc.nodes.push_back(heading);
    doc.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U"after"));
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"an item"));

    fake::FakeTextEngine engine;
    const layout::DocumentLayout laid = RunLayout(doc, 300.0f, &engine);
    EXPECT_EQ(laid.blocks.size(), static_cast<size_t>(4));
    EXPECT_EQ(engine.beginLayoutCalls, 1);
}

// A blank source line the parser keeps next to a list for the caret is
// bare in both modes: it draws no ink and charges no row, so the list's
// own gap runs straight into the next block. Editing keeps the block,
// because the caret has to be able to sit on that source line, but it is
// the same strip the reading layout charges.
TEST(ParagraphListQuote, BlankLineNextToListIsBareInBothModes) {
    fake::FakeTextEngine engine;
    Document doc;
    doc.nodes.push_back(TextNodeOf(BlockKind::List, U"one item", 0, false));
    Node blank = TextNodeOf(BlockKind::Paragraph, U"");
    blank.virtualEmptyParagraph = true;
    doc.nodes.push_back(blank);
    doc.nodes.push_back(TextNodeOf(BlockKind::Paragraph, U"after"));

    const LayoutMetrics m = BaseMetrics();
    layout::DocumentLayout reading = RunLayout(doc, 300.0f, &engine, false);
    // Two real blocks: the virtual blank is skipped entirely, not just
    // shrunk, so only the list and the paragraph take a slot.
    EXPECT_EQ(reading.blocks.size(), static_cast<size_t>(2));
    const float expectedGap =
        std::max(m.listGap, BlockTopMargin(BlockKind::Paragraph, m));
    EXPECT_NEAR(reading.blocks[1].y,
                reading.blocks[0].y + reading.blocks[0].height + expectedGap,
                0.001f);

    // Editing keeps the blank line as its own block, caret-addressable,
    // at the strip's height, and the next block's gap is still measured
    // from the list, so the two modes differ by the strip alone.
    layout::DocumentLayout editing = RunLayout(doc, 300.0f, &engine, true);
    EXPECT_EQ(editing.blocks.size(), static_cast<size_t>(3));
    EXPECT_NEAR(editing.blocks[1].height, layout::kReadBlankLineStripDip,
                0.001f);
    EXPECT_NEAR(editing.blocks[2].y,
                editing.blocks[0].y + editing.blocks[0].height +
                    layout::kReadBlankLineStripDip + expectedGap,
                0.001f);
}
