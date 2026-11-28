#include "gtest_lite.h"
#include "fake_text_engine.h"
#include "layoutcache.h"
#include "navigation.h"
#include "textbuffer.h"

TEST(Navigation, MoveLeftAscii) {
    TextBuffer b;
    b.SetText("hello");
    EXPECT_EQ(MoveLeft(b, 3), 2u);
    EXPECT_EQ(MoveLeft(b, 1), 0u);
    EXPECT_EQ(MoveLeft(b, 0), 0u);
}

TEST(Navigation, MoveRightAscii) {
    TextBuffer b;
    b.SetText("hello");
    EXPECT_EQ(MoveRight(b, 0), 1u);
    EXPECT_EQ(MoveRight(b, 4), 5u);
    EXPECT_EQ(MoveRight(b, 5), 5u);
}

TEST(Navigation, MoveLeftGrapheme) {
    TextBuffer b;
    b.SetText("ab" "\xc3\xa5");  // a-ring
    EXPECT_EQ(MoveLeft(b, 4), 2u);
    EXPECT_EQ(MoveLeft(b, 2), 1u);
}

TEST(Navigation, MoveRightGrapheme) {
    TextBuffer b;
    b.SetText("ab" "\xc3\xa5");  // a-ring
    EXPECT_EQ(MoveRight(b, 2), 4u);
    EXPECT_EQ(MoveRight(b, 0), 1u);
}

// Regression: Right from just before the last character of a paragraph
// must stop at the paragraph's own end, not jump into the next
// paragraph's first character (word-processor-guidelines.md section 3/E,
// acceptance criterion B/E). The two paragraphs are separated by a blank
// line, which is never "rendered" and used to be walked straight through.
TEST(Navigation, MoveRightStopsAtParagraphEndBeforeNextParagraph) {
    const std::string source =
        "dette er en linjeslutning.\n\ndette er naeste afsnit";
    TextBuffer b;
    b.SetText(source);

    LayoutCache cache;
    cache.SetSourceText(&source);

    BlockLayout p1;
    p1.srcOffset = 0;
    p1.srcLength = 27;  // "dette er en linjeslutning."
    for (uint32_t i = 0; i < p1.srcLength; ++i) {
        p1.u16ToSrc.push_back(i);
        p1.u16ToSrcEnd.push_back(i + 1);
    }
    cache.Add(p1);

    BlockLayout p2;
    p2.srcOffset = 29;  // past "\n\n"
    p2.srcLength = static_cast<uint32_t>(source.size()) - 29;
    for (uint32_t i = 0; i < p2.srcLength; ++i) {
        p2.u16ToSrc.push_back(29 + i);
        p2.u16ToSrcEnd.push_back(29 + i + 1);
    }
    cache.Add(p2);

    // Caret just before the final '.', at offset 26.
    EXPECT_EQ(MoveRight(b, 26, &cache), 27u);   // paragraph-end, not block 2
    EXPECT_EQ(MoveRight(b, 27, &cache), 29u);   // a further Right enters p2
}

TEST(Navigation, MoveWordLeft) {
    TextBuffer b;
    b.SetText("hello world");
    EXPECT_EQ(MoveWordLeft(b, 11), 6u);
    EXPECT_EQ(MoveWordLeft(b, 6), 0u);
}

TEST(Navigation, MoveWordRight) {
    TextBuffer b;
    b.SetText("hello world");
    EXPECT_EQ(MoveWordRight(b, 0), 5u);
    EXPECT_EQ(MoveWordRight(b, 5), 11u);
}

TEST(Navigation, MoveWordLeftPunctuation) {
    TextBuffer b;
    b.SetText("foo, bar");
    EXPECT_EQ(MoveWordLeft(b, 4), 3u);  // skip comma
    EXPECT_EQ(MoveWordLeft(b, 3), 0u);  // skip foo
}

TEST(Navigation, MoveWordRightPastSpaces) {
    TextBuffer b;
    b.SetText("hello   world");
    EXPECT_EQ(MoveWordRight(b, 5), 13u);  // skip spaces + word to end
}


TEST(Navigation, SlugifyAnchorFoldsHeadingsTheWindowsWay) {
    // Internal #links resolve through this fold, so it must match the
    // Windows slug byte for byte (link activation 4).
    EXPECT_EQ(SlugifyAnchor("Hello World"), std::string("hello-world"));
    EXPECT_EQ(SlugifyAnchor("Installation og brug!"),
              std::string("installation-og-brug"));
    EXPECT_EQ(SlugifyAnchor("  API Reference  "),
              std::string("api-reference"));
    EXPECT_EQ(SlugifyAnchor("My_Heading"), std::string("my_heading"));
    EXPECT_EQ(SlugifyAnchor("M\xc3\xa5nedens oversigt"),
              std::string("m\xc3\xa5nedens-oversigt"));
    EXPECT_EQ(SlugifyAnchor("..."), std::string(""));
}

// A second Shift+arrow steps again. The rule used to be spelled out in each
// shell, and the mac's copy answered "nothing to do" whenever a selection was
// already there: Shift+Right selected one letter and the next press moved
// nothing, which is how it was found, in a table cell.
TEST(Navigation, ASecondShiftArrowStepsAgain) {
    Selection forward;
    forward.anchor = {2};  // before the letter
    forward.active = {3};  // after it
    EXPECT_EQ(HorizontalTarget(forward, /*left=*/false, /*extend=*/true, 4), 4u);
    EXPECT_EQ(HorizontalTarget(forward, /*left=*/true, /*extend=*/true, 2), 2u);

    // Selecting backwards keeps the active end the left one, and it steps
    // left from there.
    Selection backward;
    backward.anchor = {5};
    backward.active = {3};
    EXPECT_EQ(HorizontalTarget(backward, /*left=*/true, /*extend=*/true, 2), 2u);
    EXPECT_EQ(HorizontalTarget(backward, /*left=*/false, /*extend=*/true, 4), 4u);

    // A word step is the same question with a wider step.
    EXPECT_EQ(HorizontalTarget(forward, false, true, 9), 9u);
}

// A plain arrow ends the selection at the edge it moves from, without
// stepping past it.
TEST(Navigation, APlainArrowEndsTheSelectionAtItsEdge) {
    Selection selected;
    selected.anchor = {2};
    selected.active = {7};
    EXPECT_EQ(HorizontalTarget(selected, /*left=*/true, /*extend=*/false, 1),
              2u);
    EXPECT_EQ(HorizontalTarget(selected, /*left=*/false, /*extend=*/false, 8),
              7u);

    Selection empty;
    empty.anchor = {4};
    empty.active = {4};
    EXPECT_EQ(HorizontalTarget(empty, false, false, 5), 5u);
    EXPECT_EQ(HorizontalTarget(empty, true, true, 3), 3u);
}

// ---------------------------------------------------------------------------
// The vertical step over drawn lines. These three are the behaviours the mac
// caret model pinned (`VerticalMoveEntersTheListBelow`,
// `VerticalMoveStepsThroughANestedListOneLineAtATime`,
// `VerticalCaretKnowsWhichSideOfAWrapItIsOn`). The step is one algorithm on
// both ports now, so they run here, on the cache, on every runner.
// ---------------------------------------------------------------------------

namespace {

// One laid out block for the cache: the words split into runs the fake
// engine wraps, the lines handed to the seam's own layout handle, and an
// identity UTF-16 to source map (the text is ASCII in these tests).
BlockLayout LaidBlockFor(fake::FakeTextEngine& engine, const std::string& text,
                         float x, float y, float width, uint32_t srcStart) {
    std::vector<platform::GlyphRun> runs;
    size_t at = 0;
    while (at < text.size()) {
        const size_t space = text.find(' ', at);
        const size_t end = space == std::string::npos ? text.size() : space + 1;
        platform::GlyphRun run;
        run.text = text.substr(at, end - at);
        run.font.size = 20.0f;  // the fake engine's line box is 30
        runs.push_back(run);
        at = end;
    }
    std::vector<platform::TextLine> lines =
        engine.MeasureWrapped(runs, width, 0.0f);
    float height = 0.0f;
    for (const platform::TextLine& line : lines) height += line.height;

    BlockLayout bl;
    bl.layout =
        platform::MakeLineLayout(std::move(lines), width,
                                 platform::TextAlign::Left);
    bl.x = x;
    bl.y = y;
    bl.width = width;
    bl.height = height;
    bl.srcOffset = srcStart;
    bl.srcLength = static_cast<uint32_t>(text.size());
    for (uint32_t i = 0; i < text.size(); ++i) {
        bl.u16ToSrc.push_back(srcStart + i);
        bl.u16ToSrcEnd.push_back(srcStart + i + 1);
    }
    return bl;
}

// "one two " fits 80 points (8 per glyph) and "three four" is the second
// line, so the paragraph has a soft wrap after "two ".
const char kParagraph[] = "one two three four";
constexpr uint32_t kWrapAt = 8;  // the first byte of "three"

}  // namespace

TEST(Navigation, VerticalMoveEntersTheListBelow) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.Add(LaidBlockFor(engine, kParagraph, 0.0f, 0.0f, 80.0f, 0));
    // A list item's text starts right of the margin, so the reader's own
    // column is left of it: the step has to clamp into the line.
    lc.Add(LaidBlockFor(engine, "item here", 40.0f, 60.0f, 80.0f, 100));
    float desiredX = 5.0f;
    // The paragraph's last line, one step above the list. The list's text
    // starts at x 40, right of the reader's column, and the step has to
    // clamp into it instead of skipping the block.
    const uint32_t down = MoveVertical(lc, kWrapAt + 1, 1, &desiredX, 0.0f,
                                       30.0f);
    EXPECT_EQ(down, 100u);
}

TEST(Navigation, VerticalMoveStepsThroughAListOneLineAtATime) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.Add(LaidBlockFor(engine, kParagraph, 0.0f, 0.0f, 80.0f, 0));
    lc.Add(LaidBlockFor(engine, "item here", 40.0f, 60.0f, 80.0f, 100));
    float desiredX = 5.0f;
    // From the paragraph's first line, one step down is its own second
    // line, not the list below it.
    const uint32_t down = MoveVertical(lc, 0, 1, &desiredX, 0.0f, 30.0f);
    EXPECT_GE(down, kWrapAt);
    EXPECT_LT(down, 100u);
    // And one more step reaches the list.
    const uint32_t next = MoveVertical(lc, down, 1, &desiredX, 0.0f, 30.0f);
    EXPECT_EQ(next, 100u);
}

TEST(Navigation, VerticalCaretKnowsWhichSideOfAWrapItIsOn) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.Add(LaidBlockFor(engine, "above", 0.0f, 0.0f, 80.0f, 0));
    lc.Add(LaidBlockFor(engine, kParagraph, 0.0f, 30.0f, 80.0f, 100));
    const uint32_t wrap = 100 + kWrapAt;
    float desiredX = -1.0f;
    // Drawn at the wrap's upper line, one step up leaves the block.
    const uint32_t upper = MoveVertical(lc, wrap, -1, &desiredX, 0.0f, 30.0f);
    EXPECT_EQ(upper, 0u);
    // Drawn at the wrap's lower line, the step starts there and lands on
    // the upper line of the same wrap.
    desiredX = -1.0f;
    const uint32_t lower =
        MoveVertical(lc, wrap, -1, &desiredX, 0.0f, 30.0f,
                     /*caretOnLowerLine=*/true);
    EXPECT_EQ(lower, 100u);
}

// ---------------------------------------------------------------------------
// The three caret behaviours the mac model owned alone. They are the shared
// model's now, so they are pinned here, on the cache, on every runner.
// ---------------------------------------------------------------------------

TEST(Navigation, CaretKnowsWhichSideOfAWrapItIsDrawnOn) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    // "one two " fills the first line at 8 points per glyph, so the wrap
    // position starts the second line.
    lc.Add(LaidBlockFor(engine, kParagraph, 10.0f, 0.0f, 80.0f, 0));

    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
    // The default draws the lower line: the position is the start of its
    // text, at the block's own left edge, on the second line.
    EXPECT_TRUE(lc.OffsetToCaretRect(kWrapAt, &x, &y, &h));
    EXPECT_NEAR(x, 10.0f, 0.01f);
    EXPECT_NEAR(y, 30.0f, 0.01f);

    // Told the caret is drawn at the end of the upper line, the same
    // position answers there: the full width of "one two ", on line one.
    lc.SetCaretOnLowerLine(false);
    EXPECT_TRUE(lc.OffsetToCaretRect(kWrapAt, &x, &y, &h));
    EXPECT_NEAR(x, 10.0f + 8.0f * 8.0f, 0.01f);
    EXPECT_NEAR(y, 0.0f, 0.01f);

    // A position that is not a wrap is the same on both settings.
    lc.SetCaretOnLowerLine(true);
    float px = 0.0f;
    float py = 0.0f;
    EXPECT_TRUE(lc.OffsetToCaretRect(4, &x, &y, &h));
    EXPECT_TRUE(lc.OffsetToCaretRect(4, &px, &py, &h));
    EXPECT_NEAR(x, px, 0.01f);
    EXPECT_NEAR(y, py, 0.01f);
}

TEST(Navigation, CaretAtALinkEndSkipsTheHiddenDestination) {
    // "[text](https://example.com)": the drawn word is four glyphs, mapped
    // to source bytes 1 to 4, and the caret at its visual end is source
    // byte 5, just before `](`.
    const std::string source = "[text](https://example.com)";
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.SetSourceText(&source);

    std::vector<platform::GlyphRun> runs;
    platform::GlyphRun run;
    run.text = "text";
    run.font.size = 20.0f;
    runs.push_back(run);
    std::vector<platform::TextLine> lines =
        engine.MeasureWrapped(runs, 200.0f, 0.0f);
    BlockLayout bl;
    bl.layout = platform::MakeLineLayout(
        std::move(lines), 200.0f, platform::TextAlign::Left);
    bl.width = 200.0f;
    bl.srcOffset = 1;
    bl.srcLength = 4;
    for (uint32_t i = 0; i < 4; ++i) {
        bl.u16ToSrc.push_back(1 + i);
        bl.u16ToSrcEnd.push_back(2 + i);
    }
    lc.Add(bl);

    // The rendered screen hides the destination, so the caret at the end
    // of the word steps past it and sits after the closing paren.
    EXPECT_EQ(lc.NormalizeToRenderedCaret(5),
              static_cast<uint32_t>(source.size()));

    // The source view draws the destination, so the same position stays
    // where it is: those bytes are on screen and are positions of their
    // own.
    lc.SetSyntaxRevealed(true);
    EXPECT_EQ(lc.NormalizeToRenderedCaret(5), 5u);
}

TEST(Navigation, AnEmptyCellKeepsTheCaretAtItsOwnSentinel) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;

    // A cell whose text is all hidden syntax: it owns source bytes 10 to
    // 20 and draws one sentinel position at 10.
    BlockLayout cell;
    cell.isTableCell = true;
    cell.srcOffset = 10;
    cell.srcLength = 10;
    cell.srcCellStart = 10;
    cell.srcCellEnd = 20;
    cell.u16ToSrc.push_back(10);
    cell.u16ToSrcEnd.push_back(10);
    lc.Add(cell);

    // A neighbour block with real drawn text, nearer to the far edge of
    // the cell than the cell's own start is.
    lc.Add(LaidBlockFor(engine, "next cell", 0.0f, 60.0f, 80.0f, 25));

    // A position inside the empty cell must not snap into the neighbour:
    // the sentinel is the cell's only position and the caret parks there.
    EXPECT_EQ(lc.NormalizeToRenderedCaret(19), 10u);

    // A cell with real text clamps a snapped position back to its own
    // edge instead of leaving the cell. Its text sits late in the cell
    // and the nearest drawn byte is in the block before it, so the
    // snapped answer falls outside the cell.
    LayoutCache second;
    BlockLayout late;
    late.isTableCell = true;
    late.srcOffset = 39;
    late.srcLength = 2;
    late.srcCellStart = 30;
    late.srcCellEnd = 42;
    late.u16ToSrc.push_back(39);
    late.u16ToSrcEnd.push_back(41);
    second.Add(late);
    second.Add(LaidBlockFor(engine, "before", 0.0f, 90.0f, 80.0f, 24));
    EXPECT_EQ(second.NormalizeToRenderedCaret(30), 30u);
}

TEST(Navigation, AClickInAGapLandsOnTheNearestDrawnLine) {
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.Add(LaidBlockFor(engine, "above", 0.0f, 0.0f, 80.0f, 0));
    lc.Add(LaidBlockFor(engine, "below", 0.0f, 60.0f, 80.0f, 100));

    // Ten points under the first block's bottom is nearer to it than to
    // the block below, and the x is inside the column, so the click is a
    // caret on that line.
    const uint32_t hit = lc.PointToOffsetNearestLine(8.0f, 40.0f);
    EXPECT_TRUE(hit != UINT32_MAX);
    EXPECT_EQ(LayoutCache().PointToOffsetNearestLine(8.0f, 40.0f),
              UINT32_MAX);
    EXPECT_LE(hit, 5u);
    // The left margin is not a caret.
    EXPECT_EQ(lc.PointToOffsetNearestLine(-20.0f, 10.0f), UINT32_MAX);

    // A position between two blocks has a caret rectangle of its own,
    // answered from the nearest drawn line.
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
    EXPECT_TRUE(lc.OffsetToCaretRect(50, &x, &y, &h));
}

TEST(Navigation, ARenderedStepCrossesTheHiddenMarkers) {
    // "a **b** c": the drawn characters are source bytes 0, 4 and 8.
    const std::string source = "a **b** c";
    fake::FakeTextEngine engine;
    engine.BeginLayout();
    LayoutCache lc;
    lc.SetSourceText(&source);

    std::vector<platform::GlyphRun> runs;
    platform::GlyphRun run;
    run.text = "a b c";
    run.font.size = 20.0f;
    runs.push_back(run);
    std::vector<platform::TextLine> lines =
        engine.MeasureWrapped(runs, 200.0f, 0.0f);
    BlockLayout bl;
    bl.layout = platform::MakeLineLayout(
        std::move(lines), 200.0f, platform::TextAlign::Left);
    bl.width = 200.0f;
    bl.srcLength = 9;
    // "a " at 0, the bold "b" at 4, " c" at 7 and 8, then the sentinel.
    const uint32_t visible[6] = {0, 1, 4, 7, 8, 9};
    const uint32_t ends[6] = {1, 2, 5, 8, 9, 9};
    for (int i = 0; i < 6; ++i) {
        bl.u16ToSrc.push_back(visible[i]);
        bl.u16ToSrcEnd.push_back(ends[i]);
    }
    lc.Add(bl);

    // Stepping right from the space at 1 crosses both opening markers and
    // lands on the 'b' at 4; the marker bytes are never a caret.
    EXPECT_EQ(lc.StepRendered(1, false), 4u);
    // Stepping left from the space at 7 crosses the closing markers the
    // same way.
    EXPECT_EQ(lc.StepRendered(7, true), 4u);
    // One step from the first character reaches the space after it.
    EXPECT_EQ(lc.StepRendered(0, false), 1u);
    // At the drawn text's own end there is nothing further right.
    EXPECT_EQ(lc.StepRendered(9, false), 9u);
}
