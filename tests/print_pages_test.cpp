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

// A heading with its section on the next sheet is an orphan: the reader meets
// a title and nothing under it. The cut moves up so the two travel together.
TEST(PrintPages, AHeadingIsNotLeftAloneAtTheBottom) {
    layout::LaidBlock heading = Block(800.0f, 60.0f);
    heading.kind = BlockKind::Heading;
    // The block after the heading fits a page but not the room left, so the
    // widow rule would close the page right after the heading.
    const std::vector<layout::PrintBand> bands =
        Cut({Block(0.0f, 760.0f), heading, Block(880.0f, 400.0f)});
    EXPECT_TRUE(bands.size() >= 2);
    if (bands.size() < 2) return;
    // The first page ends where the heading begins, not after it.
    NEAR(bands[0].bottom, 800.0f);
    // And the second page starts with it.
    NEAR(bands[1].top, 800.0f);
}

// A heading that is the page's first block is not moved: that page would be
// empty and the walk would never leave it.
TEST(PrintPages, AHeadingThatStartsAPageStaysOnIt) {
    layout::LaidBlock heading = Block(0.0f, 60.0f);
    heading.kind = BlockKind::Heading;
    const std::vector<layout::PrintBand> bands =
        Cut({heading, Block(60.0f, 1500.0f)});
    EXPECT_TRUE(!bands.empty());
    if (bands.empty()) return;
    NEAR(bands[0].top, 0.0f);
    NEAR(bands[0].bottom, kH);
}

// A page is its band, at the page's margin. The placement takes the band's
// own distance from the document's top back out, which is what makes the
// second page show the second page's lines instead of the beginning of the
// document again: a slice keeps document coordinates.
TEST(PrintPages, PlacementPutsTheBandOnItsPage) {
    const layout::PrintPage page;
    const std::vector<layout::PrintBand> bands =
        Cut({Block(0.0f, 900.0f), Block(1000.0f, 900.0f)});
    EXPECT_EQ(bands.size(), size_t(2));

    const layout::PagePlacement first = layout::PlacementForBand(page, bands[0]);
    NEAR(first.x, page.margin);
    NEAR(first.y, page.margin);

    const layout::PagePlacement second =
        layout::PlacementForBand(page, bands[1]);
    NEAR(second.y, page.margin - bands[1].top);
    EXPECT_TRUE(second.y < first.y);

    // And the arithmetic lands the band's own item inside the page: its top
    // below the margin and its bottom above the paper's edge.
    display::DisplayList list;
    list.width = 200.0f;
    list.height = 1900.0f;
    list.items.push_back(Box(1500.0f, 40.0f));
    const display::DisplayList slice = layout::SliceForBand(list, bands[1]);
    EXPECT_EQ(slice.items.size(), size_t(1));
    const float onPage = display::ItemBounds(slice.items[0]).y + second.y;
    EXPECT_TRUE(onPage >= page.margin - 0.001f);
    EXPECT_TRUE(onPage + 40.0f <= page.height - page.margin + 0.001f);
}

// A table's words are not in the page list: the window paints them from the
// table's own record, with selection and find marks between the lines. A page
// replay has no window, so the page brings the record's copy with it; without
// it a printed table is a grid of empty boxes, which is exactly what a reader
// of this document saw.
TEST(PrintPages, APageCarriesItsTablesWords) {
    Document doc;
    ParseMarkdown(
        "Intro line.\n\n"
        "| Column one | Column two |\n"
        "|---|---|\n"
        "| A cell with words | Another cell |\n"
        "| Second row | More words |\n",
        doc);

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

    const layout::LaidBlock* table = nullptr;
    for (const layout::LaidBlock& b : laid.blocks) {
        if (b.table) table = &b;
    }
    EXPECT_TRUE(table != nullptr);
    if (!table) return;

    // The record holds one clipped run of lines per cell.
    size_t texts = 0;
    size_t pushes = 0;
    size_t pops = 0;
    for (const display::Item& item : table->table->cellsText.items) {
        if (std::holds_alternative<display::Text>(item)) ++texts;
        if (std::holds_alternative<display::PushClip>(item)) ++pushes;
        if (std::holds_alternative<display::PopClip>(item)) ++pops;
    }
    EXPECT_TRUE(texts >= 4);
    EXPECT_EQ(pushes, pops);
    EXPECT_EQ(pushes, size_t(6));  // two header cells and four body cells

    // And the page's own items carry them, so a replay draws the table.
    const auto bands = layout::PaginateDocument(laid, layout::PrintPage());
    EXPECT_TRUE(!bands.empty());
    if (bands.empty()) return;
    const display::DisplayList page = layout::PageItems(laid, bands[0]);
    size_t pageTexts = 0;
    for (const display::Item& item : page.items) {
        if (std::holds_alternative<display::Text>(item)) ++pageTexts;
    }
    EXPECT_TRUE(pageTexts >= texts);
}

// Paper is not a window: a page sets smaller type than the reading view, and
// it sets the spacing that belongs to that type, or the column reads airy.
TEST(PrintPages, PaperSetsSmallerTypeThanTheReadingView) {
    const LayoutMetrics screen = ScaleMetrics(BaseMetrics(), 1.0f);
    const LayoutMetrics paper = PaperMetrics(BaseMetrics());
    NEAR(paper.bodyFontSize, screen.bodyFontSize * 5.0f / 6.0f);
    EXPECT_TRUE(paper.bodyFontSize < screen.bodyFontSize);
    NEAR(paper.codeFontSize, screen.codeFontSize * 5.0f / 6.0f);
    // The spacing shrinks with it, so a page is not a small column with large
    // gaps.
    NEAR(paper.paraGap, screen.paraGap * 5.0f / 6.0f);
    EXPECT_TRUE(paper.paraGap < screen.paraGap);
    // And the page's width is the walk's, not a reading cap.
    EXPECT_TRUE(paper.maxContentWidth > 10000.0f);
}

// A title must not be a screen title on paper. The cap is relative to the
// paper body, so a style with large headings is tamed while a compact style
// keeps its own numbers.
TEST(PrintPages, PaperHeadingsKeepAReadableHierarchy) {
    const float caps[7] = {0.0f, 1.8f, 1.45f, 1.2f, 1.1f, 1.0f, 1.0f};
    for (const LayoutMetrics base : {GitHubMetrics(), WikipediaMetrics(),
                                     AppleMetrics()}) {
        const LayoutMetrics paper = PaperMetrics(base);
        for (int level = 1; level <= 6; ++level) {
            EXPECT_TRUE(paper.headingSizes[level] <=
                        paper.bodyFontSize * caps[level] + 0.001f);
            EXPECT_TRUE(paper.headingSizes[level] >=
                        (std::min)(base.headingSizes[level] * 5.0f / 6.0f,
                                   paper.bodyFontSize * caps[level]) - 0.001f);
        }
        EXPECT_TRUE(paper.headingSizes[1] > paper.headingSizes[2]);
        EXPECT_TRUE(paper.headingSizes[2] > paper.headingSizes[3]);
    }
    const LayoutMetrics apple = PaperMetrics(AppleMetrics());
    EXPECT_TRUE(apple.headingSizes[1] < 20.0f);
    EXPECT_TRUE(apple.headingSizes[2] < 16.0f);
    NEAR(apple.headingSizes[1], apple.bodyFontSize * 1.8f);
}
