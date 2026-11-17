// The pagination and slice suite of the printing plan (task 1.4). The page
// model is pure geometry, so the cases below build blocks with the numbers the
// rule is about: a page that fills exactly, a widow, a block taller than the
// paper, and a rule that sits on a cut line.
//
// The last case lays out a real document with the fake engine, so the shared
// walk and the cut are exercised together, the way the print path runs them.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "layout/print_pages.h"
#include "parser.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

#define NEAR(a, b) EXPECT_NEAR(a, b, 0.001f)

const LayoutMetrics kM = BaseMetrics();
const Palette kPal = BasePalette();
const float kH = layout::PrintPage().ContentHeight();   // 971.3 seam units
const float kW = layout::PrintPage().ContentWidth();    // 642.5 seam units
const float kPt = 96.0f / 72.0f;

layout::LaidBlock Block(float y, float height) {
    layout::LaidBlock b;
    b.kind = BlockKind::Paragraph;
    b.y = y;
    b.height = height;
    return b;
}

layout::DocumentLayout Layout(std::vector<layout::LaidBlock> blocks) {
    layout::DocumentLayout laid;
    laid.blocks = std::move(blocks);
    return laid;
}

// The bands, for a list of blocks on A4.
std::vector<layout::PrintBand> Cut(const std::vector<layout::LaidBlock>& b) {
    return layout::PaginateDocument(Layout(b), layout::PrintPage());
}

bool StartsInside(const std::vector<layout::PrintBand>& bands, float top,
                  float bottom) {
    for (const layout::PrintBand& band : bands) {
        if (band.top > top + 0.001f && band.top < bottom - 0.001f) return true;
    }
    return false;
}

display::Item Rule(float y) {
    display::StrokeLine line;
    line.x0 = 0.0f;
    line.x1 = 200.0f;
    line.y0 = y;
    line.y1 = y;
    line.lineWidth = 1.0f;
    return line;
}

display::Item Box(float y, float height) {
    display::FillRect fill;
    fill.rect = platform::Rect{0.0f, y, 200.0f, height};
    fill.color = platform::Color{0.0f, 0.0f, 0.0f, 1.0f};
    return fill;
}

size_t CountOf(const display::DisplayList& list, bool push) {
    size_t n = 0;
    for (const display::Item& item : list.items) {
        const bool isPush = std::holds_alternative<display::PushClip>(item);
        const bool isPop = std::holds_alternative<display::PopClip>(item);
        if ((push && isPush) || (!push && isPop)) ++n;
    }
    return n;
}

}  // namespace

TEST(PrintPages, AnEmptyDocumentIsOnePage) {
    const auto bands = Cut({});
    EXPECT_EQ(bands.size(), size_t(1));
    NEAR(bands[0].top, 0.0f);
    NEAR(bands[0].bottom, kH);
    EXPECT_EQ(bands[0].firstBlock, size_t(0));
    EXPECT_EQ(bands[0].lastBlock, size_t(0));
}

TEST(PrintPages, AShortPageIsAtLeastOnePage) {
    const auto bands = Cut({Block(0.0f, 40.0f)});
    EXPECT_EQ(bands.size(), size_t(1));
    NEAR(bands[0].bottom, kH);
    EXPECT_TRUE(bands[0].bottom >= kH);
    EXPECT_EQ(bands[0].lastBlock, size_t(1));
}

TEST(PrintPages, BlocksThatFillThePageStartTheSecondOnTheNextOne) {
    // Two halves make the page exactly, and the third block starts on the
    // cut, where the second page begins.
    const auto bands = Cut(
        {Block(0.0f, kH * 0.5f), Block(kH * 0.5f, kH * 0.5f),
         Block(kH, kH * 0.5f), Block(kH * 1.5f, 10.0f)});
    EXPECT_EQ(bands.size(), size_t(2));
    NEAR(bands[0].top, 0.0f);
    NEAR(bands[0].bottom, kH);
    NEAR(bands[1].top, kH);
    // The block on the cut is the first one of the second page.
    EXPECT_EQ(bands[0].lastBlock, size_t(2));
    EXPECT_EQ(bands[1].firstBlock, size_t(2));
    EXPECT_EQ(bands[1].lastBlock, size_t(4));
}

TEST(PrintPages, ABlockThatDoesNotFitTheRoomLeftMovesWhole) {
    // 600 + 500 does not fit 971.3, and 500 fits an empty page: the block
    // moves, and no band begins inside it.
    const std::vector<layout::LaidBlock> blocks = {Block(0.0f, 600.0f),
                                                   Block(600.0f, 500.0f)};
    const auto bands = Cut(blocks);
    EXPECT_EQ(bands.size(), size_t(2));
    NEAR(bands[0].bottom, 600.0f);
    NEAR(bands[1].top, 600.0f);
    EXPECT_TRUE(!StartsInside(bands, 600.0f, 1100.0f));
    EXPECT_EQ(bands[1].firstBlock, size_t(1));
}

TEST(PrintPages, ABlockTallerThanAPageIsCutAndContinues) {
    const auto bands = Cut({Block(0.0f, 2000.0f)});
    EXPECT_EQ(bands.size(), size_t(3));
    NEAR(bands[0].bottom, kH);
    NEAR(bands[1].top, kH);
    NEAR(bands[1].bottom, kH * 2.0f);
    NEAR(bands[2].top, kH * 2.0f);
    // The block is the last one the band touches, on every page.
    for (const layout::PrintBand& band : bands) {
        EXPECT_EQ(band.firstBlock, size_t(0));
        EXPECT_EQ(band.lastBlock, size_t(1));
    }
}

TEST(PrintPages, ThirtyParagraphsMakeTheCeilingOfThePageCount) {
    std::string markdown;
    for (int i = 0; i < 30; ++i) {
        markdown += "Paragraph number " + std::to_string(i) +
                    " with a line of words in it.\n\n";
    }
    Document doc;
    ParseMarkdown(markdown, doc);

    fake::FakeTextEngine engine;
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &kM;
    in.palette = &kPal;
    in.contentWidth = kW;
    in.zoom = 1.0f;
    in.editing = false;
    in.engine = &engine;
    const layout::DocumentLayout laid = layout::LayoutDocument(in);
    EXPECT_TRUE(!laid.blocks.empty());

    const auto bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    float total = 0.0f;
    for (const layout::LaidBlock& b : laid.blocks) {
        total = (std::max)(total, b.y + b.height);
    }
    const size_t expected =
        static_cast<size_t>(std::ceil(total / kH - 0.0001f));
    EXPECT_EQ(bands.size(), expected == 0 ? size_t(1) : expected);
    for (size_t i = 0; i < bands.size(); ++i) {
        EXPECT_TRUE(bands[i].bottom > bands[i].top);
        if (i > 0) NEAR(bands[i].top, bands[i - 1].bottom);
    }
}

TEST(PrintPages, ASliceKeepsEveryClipPairAndDropsTheOtherPage) {
    display::DisplayList list;
    list.width = 400.0f;
    list.height = kH * 2.0f;
    // A clip around the first page, and one around the second.
    list.items.push_back(display::PushClip{platform::Rect{0.0f, 0.0f, 400.0f, kH}});
    list.items.push_back(Box(10.0f, 20.0f));
    list.items.push_back(Box(500.0f, 20.0f));
    list.items.push_back(display::PopClip{});
    list.items.push_back(display::PushClip{platform::Rect{0.0f, kH, 400.0f, kH}});
    list.items.push_back(Box(kH + 10.0f, 20.0f));
    list.items.push_back(display::PopClip{});
    // A clip that belongs to no page at all still travels with every page:
    // the player pairs pushes and pops, and a page missing half a pair
    // clips the rest of its own drawing away.
    list.items.push_back(display::PushClip{platform::Rect{0.0f, 9000.0f, 400.0f, 50.0f}});
    list.items.push_back(display::PopClip{});

    const auto bands = Cut({Block(0.0f, 10.0f), Block(kH, 10.0f)});
    EXPECT_EQ(bands.size(), size_t(2));
    const display::DisplayList first = layout::SliceForBand(list, bands[0]);
    const display::DisplayList second = layout::SliceForBand(list, bands[1]);

    EXPECT_EQ(CountOf(first, true), CountOf(first, false));
    EXPECT_EQ(CountOf(second, true), CountOf(second, false));
    EXPECT_EQ(CountOf(first, true), size_t(3));
    EXPECT_EQ(CountOf(second, true), size_t(3));
    // Three boxes in all; no page keeps every one of them.
    const size_t firstBoxes = first.items.size() - CountOf(first, true) * 2;
    const size_t secondBoxes =
        second.items.size() - CountOf(second, true) * 2;
    EXPECT_EQ(firstBoxes + secondBoxes, size_t(3));
    EXPECT_TRUE(firstBoxes < 3);
    EXPECT_TRUE(secondBoxes < 3);
    NEAR(first.height, kH);
}

TEST(PrintPages, AnItemOnTheCutLineIsOnExactlyOnePage) {
    display::DisplayList list;
    list.width = 400.0f;
    // A filled box that begins exactly on the cut, and a hairline rule there.
    list.items.push_back(Box(kH, 20.0f));
    list.items.push_back(Rule(kH));

    const auto bands = Cut({Block(0.0f, kH), Block(kH, 20.0f)});
    EXPECT_EQ(bands.size(), size_t(2));
    const display::DisplayList first = layout::SliceForBand(list, bands[0]);
    const display::DisplayList second = layout::SliceForBand(list, bands[1]);
    EXPECT_EQ(first.items.size(), size_t(0));
    EXPECT_EQ(second.items.size(), size_t(2));
}
