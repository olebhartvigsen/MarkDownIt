// The app layer's text engine, measured directly.
//
// The Core Text engine is the one piece of the mac app that the shared
// test suite cannot reach, because it lives in the app layer and talks to
// Core Text. The first real document open faulted inside its attribute
// calls, so the engine gets these tests: the measure path runs on the
// build machine from now on, over the same attributed string the app
// builds.

#include "gtest_lite.h"

#include "../src/macos/seams.h"
#include "../src/macos/text_engine_mac.h"

using mac::CoreTextEngine;
using mac::FontSpec;
using mac::GlyphRun;
using mac::TextLine;

namespace {

GlyphRun Run(const std::string& text, bool bold = false) {
    GlyphRun run;
    run.text = text;
    run.font.family = "Helvetica Neue";
    run.font.size = 16.0f;
    run.font.bold = bold;
    run.r = 0.2f;
    run.g = 0.3f;
    run.b = 0.4f;
    return run;
}

std::string Rebuild(const std::vector<TextLine>& lines) {
    std::string text;
    for (const TextLine& line : lines) text += CoreTextEngine::LineTextUtf8(line);
    return text;
}

}  // namespace

TEST(TextEngineMac, MeasuresOneRun) {
    CoreTextEngine engine;
    const std::vector<TextLine> lines =
        engine.MeasureWrapped({Run("Hello, markdown.")}, 10000.0f, 0.0f);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_GT(lines[0].width, 0.0f);
    EXPECT_GT(lines[0].height, 0.0f);
    ASSERT_EQ(lines[0].runs.size(), 1u);
    EXPECT_EQ(CoreTextEngine::LineTextUtf8(lines[0]),
              std::string("Hello, markdown."));
    EXPECT_NEAR(lines[0].runs[0].x, 0.0f, 0.01f);
}

TEST(TextEngineMac, MeasuresSeveralStyledRuns) {
    CoreTextEngine engine;
    const std::vector<TextLine> lines = engine.MeasureWrapped(
        {Run("plain "), Run("bold", true), Run(" tail")}, 10000.0f, 0.0f);
    ASSERT_EQ(lines.size(), 1u);
    ASSERT_EQ(lines[0].runs.size(), 3u);
    EXPECT_EQ(CoreTextEngine::LineTextUtf8(lines[0]),
              std::string("plain bold tail"));
    // The runs advance along the line in order.
    EXPECT_TRUE(lines[0].runs[1].x >= lines[0].runs[0].x);
    EXPECT_TRUE(lines[0].runs[2].x >= lines[0].runs[1].x);
}

TEST(TextEngineMac, WrapsALongRunAndKeepsEveryCharacter) {
    CoreTextEngine engine;
    std::string text;
    for (int i = 0; i < 40; ++i) text += "word ";
    text += "end";
    const std::vector<TextLine> lines =
        engine.MeasureWrapped({Run(text)}, 140.0f, 0.0f);
    ASSERT_GE(lines.size(), 2u);
    // Every line carries the slice of the run that sits on it, and the
    // slices together are the whole text.
    EXPECT_EQ(Rebuild(lines), text);
    for (const TextLine& line : lines) {
        EXPECT_TRUE(!line.runs.empty());
        EXPECT_LT(line.width, 145.0f);
    }
}

TEST(TextEngineMac, WrapsMultibyteTextWithoutLosingCharacters) {
    CoreTextEngine engine;
    std::string text;
    for (int i = 0; i < 12; ++i) text += "\u00e6\u00f8\u00e5 ";
    const std::vector<TextLine> lines =
        engine.MeasureWrapped({Run(text)}, 70.0f, 0.0f);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(Rebuild(lines), text);
}

TEST(TextEngineMac, HitTestsAndPlacesOffsets) {
    CoreTextEngine engine;
    const std::vector<TextLine> lines =
        engine.MeasureWrapped({Run("offset test")}, 10000.0f, 0.0f);
    ASSERT_EQ(lines.size(), 1u);

    bool trailing = false;
    EXPECT_EQ(engine.HitTestOffset(lines[0], 0.0f, &trailing), 0u);
    const uint32_t middle =
        engine.HitTestOffset(lines[0], lines[0].width * 0.5f, &trailing);
    EXPECT_GT(middle, 0u);
    EXPECT_LT(middle, static_cast<uint32_t>(std::string("offset test").size()));

    const mac::Rect rect = engine.OffsetToRect(lines[0], 6);
    EXPECT_TRUE(rect.x >= 0.0f);
    EXPECT_GT(rect.height, 0.0f);
}
