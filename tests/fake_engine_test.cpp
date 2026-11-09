// The fake engine deterministic behavior of plan task 2.3. Everything
// asserted here is what the cross runner goldens will rely on, so a
// change to any of these numbers changes goldens, on purpose.

#include "fake_text_engine.h"
#include "gtest_lite.h"

#include <vector>

using fake::FakeTextEngine;
using platform::FontSpec;
using platform::GlyphRun;
using platform::TextLine;

namespace {

GlyphRun MakeRun(const std::string& text, float size = 14.0f) {
    GlyphRun run;
    run.text = text;
    run.font.size = size;
    return run;
}

FontSpec MakeFont(float size = 14.0f) {
    FontSpec font;
    font.size = size;
    return font;
}

}  // namespace

TEST(FakeEngine, GlyphWidthIsFixedPerCodePoint) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("hello")};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(lines.size(), static_cast<size_t>(1));
    EXPECT_NEAR(lines[0].width, 5 * FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(lines[0].runs[0].x, 0.0f, 0.001f);
}

TEST(FakeEngine, MultibyteCountsAsOneGlyph) {
    FakeTextEngine engine;
    // a (1) + Ã© (2 bytes) + â€œÃ¥ (6 bytes) = 7 code points, 9 bytes.
    std::vector<GlyphRun> runs = {MakeRun("a\xC3\xA9\xF0\x9F\x8E\x89")};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    // a + é + U+1F389 = 3 code points.
    EXPECT_NEAR(lines[0].width, 3 * FakeTextEngine::kGlyphWidth, 0.001f);
}

TEST(FakeEngine, WideRunMovesToTheNextLineWhole) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("aaaa"), MakeRun("bb"),
                                 MakeRun("ccc")};
    // Room for six glyphs; the second run fits on the first line, the
    // third does not and moves down with its start at x zero.
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 6 *
                                    FakeTextEngine::kGlyphWidth, 0.0f);
    EXPECT_EQ(lines.size(), static_cast<size_t>(2));
    EXPECT_NEAR(lines[0].width, 6 * FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(lines[1].width, 3 * FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(lines[0].runs[1].x, 4 * FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(lines[1].runs[0].x, 0.0f, 0.001f);
}

TEST(FakeEngine, OversizedFirstRunKeepsItsOwnLine) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("aaaaaaaa")};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 2 *
                                    FakeTextEngine::kGlyphWidth, 0.0f);
    EXPECT_EQ(lines.size(), static_cast<size_t>(1));
    EXPECT_NEAR(lines[0].width, 8 * FakeTextEngine::kGlyphWidth, 0.001f);
}

TEST(FakeEngine, FontSpacedRunsStayOnOneLineWhenTheyFit) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("ab", 20.0f), MakeRun("cd", 10.0f)};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 100 *
                                    FakeTextEngine::kGlyphWidth, 0.0f);
    EXPECT_EQ(lines.size(), static_cast<size_t>(1));
    // The widest font decides the line height, the theme's ratio.
    EXPECT_NEAR(lines[0].height, 20.0f * 1.5f, 0.001f);
}

TEST(FakeEngine, LineHeightFollowsSize) {
    FakeTextEngine engine;
    EXPECT_NEAR(engine.LineHeight(MakeFont(12.0f)), 18.0f, 0.001f);
    EXPECT_NEAR(engine.LineHeight(MakeFont(24.0f)), 36.0f, 0.001f);
}

TEST(FakeEngine, HitTestRoundsToTheNearestGlyph) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("abcd")};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    bool trailing = false;
    // The middle of glyph two, b: still inside b, trailing half.
    EXPECT_EQ(engine.HitTestOffset(lines[0], 1.6f * FakeTextEngine::kGlyphWidth,
                                   &trailing),
              static_cast<uint32_t>(1));
    EXPECT_TRUE(trailing);
    // The left edge of glyph three.
    trailing = true;
    EXPECT_EQ(engine.HitTestOffset(lines[0], 2.0f * FakeTextEngine::kGlyphWidth,
                                   &trailing),
              static_cast<uint32_t>(2));
    EXPECT_TRUE(!trailing);
    // Past the end: the last position.
    EXPECT_EQ(engine.HitTestOffset(lines[0], 99.0f, &trailing),
              static_cast<uint32_t>(4));
}

TEST(FakeEngine, OffsetToRectLandsOnTheGlyph) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs = {MakeRun("abcd")};
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    platform::Rect rect = engine.OffsetToRect(lines[0], 2);
    EXPECT_NEAR(rect.x, 2 * FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(rect.width, FakeTextEngine::kGlyphWidth, 0.001f);
    EXPECT_NEAR(rect.height, lines[0].height, 0.001f);
    // Past the end: the trailing edge.
    rect = engine.OffsetToRect(lines[0], 40);
    EXPECT_NEAR(rect.x, 4 * FakeTextEngine::kGlyphWidth, 0.001f);
}

TEST(FakeEngine, EmptyRunsDrawOneEmptyLine) {
    FakeTextEngine engine;
    std::vector<GlyphRun> runs;
    std::vector<TextLine> lines = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(lines.size(), static_cast<size_t>(1));
    EXPECT_NEAR(lines[0].width, 0.0f, 0.001f);
    EXPECT_TRUE(lines[0].runs.empty());
    // A caret placed at offset zero is still a real rect.
    platform::Rect rect = engine.OffsetToRect(lines[0], 0);
    EXPECT_NEAR(rect.width, FakeTextEngine::kGlyphWidth, 0.001f);
}
