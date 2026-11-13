// The seam conformance suite of plan task 2.2. One set of assertions,
// every engine that implements the seam. The fake engine of task 2.3
// runs it on both platforms, the Core Text engine on macOS; the
// DirectWrite engine enrolls when it lands, and each future engine
// joins with one line.
//
// The assertions are structural invariants, not exact numbers: real
// engines answer with real fonts, so the suite checks what every
// conforming engine owes its callers, whatever glyph widths it picks.

#include "fake_text_engine.h"
#include "gtest_lite.h"

#include <string>
#include <vector>

#ifdef __APPLE__
#include "../src/macos/text_engine_mac.h"
#endif

#ifdef _WIN32
#include "../src/windows/text_engine_win.h"
#endif

using platform::FontSpec;
using platform::GlyphRun;
using platform::TextEngine;
using platform::TextLine;

namespace {

GlyphRun MakeRun(const std::string& text, float size = 14.0f) {
    GlyphRun run;
    run.text = text;
    run.font.size = size;
    run.font.family = "Body";
    return run;
}

FontSpec MakeFont(float size = 14.0f) {
    FontSpec font;
    font.size = size;
    font.family = "Body";
    return font;
}

// What every conforming engine owes its callers. label only names the
// engine in the failure output.
void CheckTextEngine(TextEngine& engine) {
    // Line height is deterministic and follows the size.
    const float h1 = engine.LineHeight(MakeFont(14.0f));
    const float h1again = engine.LineHeight(MakeFont(14.0f));
    EXPECT_NEAR(h1, h1again, 0.001f);
    const float h2 = engine.LineHeight(MakeFont(24.0f));
    EXPECT_TRUE(h2 >= h1);

    std::vector<GlyphRun> runs = {MakeRun("first words", 14.0f),
                                  MakeRun("second", 14.0f)};

    // A maxWidth of zero means one unbounded line, and every run stays
    // on it in order, at nondecreasing x.
    engine.BeginLayout();
    std::vector<TextLine> unbounded = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(unbounded.size(), static_cast<size_t>(1));
    EXPECT_EQ(unbounded[0].runs.size(), static_cast<size_t>(2));
    size_t glyphs = 0;
    for (const TextLine& line : unbounded) {
        float x = -1.0f;
        for (const GlyphRun& run : line.runs) {
            EXPECT_TRUE(run.x >= x);
            x = run.x;
            for (unsigned char c : run.text) {
                if ((c & 0xC0) != 0x80) ++glyphs;
            }
        }
    }
    EXPECT_TRUE(glyphs >= 11);  // first words alone carries it
    EXPECT_TRUE(unbounded[0].width >= 11 * 2);

    // Measuring is deterministic pass to pass.
    engine.BeginLayout();
    std::vector<TextLine> again = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(again.size(), unbounded.size());
    EXPECT_NEAR(again[0].width, unbounded[0].width, 0.001f);

    // A bounded width keeps every character, in order, and wrapping
    // never makes the text wider than the unbounded line was. Engines
    // may merge runs that share a style or split a run at a line break,
    // so the grid counts code points, not run entries: whatever the
    // engine did to the runs, the reading order and the ink must
    // survive the wrap.
    std::vector<GlyphRun> many = {MakeRun("aaaa"), MakeRun("bbbb"),
                                  MakeRun("cccc")};
    const float bound = 0.75f * unbounded[0].width;
    ASSERT_TRUE(bound > 0.0f);
    engine.BeginLayout();
    std::vector<TextLine> wrapped = engine.MeasureWrapped(many, bound, 0.0f);
    std::string wrappedText;
    float wrappedWidth = 0.0f;
    for (const TextLine& line : wrapped) {
        float x = -1.0f;
        for (const GlyphRun& run : line.runs) {
            wrappedText += run.text;
            EXPECT_TRUE(run.x >= x);
            x = run.x;
        }
        wrappedWidth += line.width;
        EXPECT_GT(line.height, 0.0f);
    }
    std::string sourceText;
    for (const GlyphRun& run : many) sourceText += run.text;
    EXPECT_EQ(wrappedText, sourceText);
    EXPECT_LE(wrappedWidth, unbounded[0].width + 0.001f);

    // A caret inside a wrapped line sits inside that line. The engines
    // that own their layouts answer positions in the paragraph's index
    // space, so a caller that speaks line-relative units gets that line's
    // first byte back for every offset, and the caret never moves into the
    // line. The second line is the case: the first one starts at zero and
    // hides the mistake. The bound is a fraction of this text's own
    // unbounded width, so the wrap happens whatever glyph widths the
    // engine picked.
    std::vector<GlyphRun> wrapping = {MakeRun("aaaa"), MakeRun("bbbb"),
                                      MakeRun("cccc")};
    engine.BeginLayout();
    std::vector<TextLine> whole = engine.MeasureWrapped(wrapping, 0.0f, 0.0f);
    ASSERT_TRUE(whole.size() == static_cast<size_t>(1));
    ASSERT_TRUE(whole[0].width > 3.0f);
    engine.BeginLayout();
    std::vector<TextLine> wrappedTwice =
        engine.MeasureWrapped(wrapping, whole[0].width * 0.6f, 0.0f);
    ASSERT_TRUE(wrappedTwice.size() >= static_cast<size_t>(2));
    const TextLine& secondLine = wrappedTwice[1];
    const platform::Rect secondStart = engine.OffsetToRect(secondLine, 0);
    const platform::Rect secondInside = engine.OffsetToRect(secondLine, 2);
    EXPECT_GT(secondInside.x, secondStart.x + 0.5f);

    // Word wrapping breaks lines at spaces, and the space that carries
    // the break still has to advance the ink after it: a line laid
    // run by run at trimmed widths drops every space at a run's end,
    // and the words come out glued. The grid pins the gap between two
    // runs sharing a wrapped line, with the bound measured off the
    // engine's own unbounded answer so metric guesses stay out. Runs
    // one and two fit the bound together, run three does not.
    std::vector<GlyphRun> parted = {MakeRun("aa ", 14.0f),
                                   MakeRun("bb ", 14.0f),
                                   MakeRun("ccc", 14.0f)};
    std::vector<GlyphRun> pair = {parted[0], parted[1]};
    engine.BeginLayout();
    std::vector<TextLine> pairLines = engine.MeasureWrapped(pair, 0.0f, 0.0f);
    ASSERT_TRUE(pairLines.size() >= 1);
    ASSERT_TRUE(pairLines[0].width > 1.0f);
    const float pairBound = pairLines[0].width + 0.5f;
    engine.BeginLayout();
    std::vector<TextLine> partedLines =
        engine.MeasureWrapped(parted, pairBound, 0.0f);
    ASSERT_TRUE(partedLines.size() >= 2);  // the third run did not fit
    std::string partedText;
    for (const TextLine& line : partedLines) {
        const GlyphRun* previous = nullptr;
        for (const GlyphRun& run : line.runs) {
            if (previous) {
                // The space between the runs still advances: a gap a
                // reader can see, not the trimmed-width overlap.
                EXPECT_GT(run.x, previous->x + 1.0f);
            }
            partedText += run.text;
            previous = &run;
        }
    }
    std::string partedSource;
    for (const GlyphRun& run : parted) partedSource += run.text;
    EXPECT_EQ(partedText, partedSource);

    // Offsets are UTF-8 bytes: the multibyte line draws four
    // characters in eight bytes, and the trailing hit answers the
    // byte count, not the character count.
    std::vector<GlyphRun> wide = {MakeRun("a\xC3\xA9\xF0\x9F\x8E\x89"
                                         "b")};
    engine.BeginLayout();
    std::vector<TextLine> wideLines =
        engine.MeasureWrapped(wide, 0.0f, 0.0f);
    const platform::Rect wideEnd = engine.OffsetToRect(wideLines[0], 8);
    bool wideTrailing = false;
    const uint32_t wideHit = engine.HitTestOffset(wideLines[0],
                                                  wideEnd.x + 999.0f,
                                                  &wideTrailing);
    EXPECT_EQ(wideHit, static_cast<uint32_t>(8));

    // A measured line carries the engine's own handle on it: set, unique
    // within a pass, and kept by a copy, which is how every reader holds
    // the line. Matching a line by its width and its text is not an
    // identity, and a stale entry that shares the width alone answers the
    // line's first byte for every offset, so the caret sits at the start
    // of the line no matter where the click went.
    engine.BeginLayout();
    std::vector<TextLine> keyed = engine.MeasureWrapped(parted, 0.0f, 0.0f);
    ASSERT_TRUE(keyed.size() >= 1);
    for (size_t i = 0; i < keyed.size(); ++i) {
        EXPECT_TRUE(keyed[i].measuredKey != 0u);
        const TextLine copy = keyed[i];
        EXPECT_EQ(copy.measuredKey, keyed[i].measuredKey);
        for (size_t j = i + 1; j < keyed.size(); ++j) {
            EXPECT_TRUE(keyed[i].measuredKey != keyed[j].measuredKey);
        }
    }

    // A line of several runs, the shape every paragraph with a styled
    // span has. A caret offset is a byte inside the line, so its rect only
    // moves right as the offset grows, across a run boundary as well as
    // inside a run. A paragraph map that skips a run's worth of bytes
    // makes every offset past that run answer its last byte, and the
    // caret stops at the end of the styled span.
    std::vector<GlyphRun> styled = {MakeRun("plain ", 14.0f),
                                    MakeRun("code", 12.0f),
                                    MakeRun(" after", 14.0f)};
    engine.BeginLayout();
    std::vector<TextLine> styledLines =
        engine.MeasureWrapped(styled, 0.0f, 0.0f);
    ASSERT_TRUE(styledLines.size() == static_cast<size_t>(1));
    const TextLine& styledLine = styledLines[0];
    float previousX = -1.0f;
    for (uint32_t byte = 0; byte <= 16; ++byte) {
        const platform::Rect at = engine.OffsetToRect(styledLine, byte);
        EXPECT_TRUE(at.x >= previousX - 0.01f);
        previousX = at.x;
    }
    const platform::Rect styledEnd = engine.OffsetToRect(styledLine, 16);
    EXPECT_TRUE(styledEnd.x > 1.0f);
    bool styledTrailing = false;
    EXPECT_EQ(engine.HitTestOffset(styledLine, styledEnd.x + 500.0f,
                                   &styledTrailing),
              static_cast<uint32_t>(16));

    // Hit test and caret rect agree about position and direction.
    const TextLine& line = unbounded[0];
    const platform::Rect atZero = engine.OffsetToRect(line, 0);
    const platform::Rect atEnd = engine.OffsetToRect(line, 40);
    EXPECT_TRUE(atEnd.x >= atZero.x);
    bool trailing = true;
    const uint32_t hitZero = engine.HitTestOffset(line, atZero.x, &trailing);
    EXPECT_TRUE(hitZero <= 1u);
    const uint32_t hitEnd =
        engine.HitTestOffset(line, atEnd.x + 999.0f, &trailing);
    EXPECT_TRUE(hitEnd >= hitZero);
}

}  // namespace

TEST(SeamConformance, FakeEngineFollowsTheSeam) {
    fake::FakeTextEngine engine;
    CheckTextEngine(engine);
}

#ifdef __APPLE__
TEST(SeamConformance, CoreTextEngineFollowsTheSeam) {
    platform::CoreTextEngine engine;
    CheckTextEngine(engine);
}
#endif

#ifdef _WIN32
TEST(SeamConformance, DWriteEngineFollowsTheSeam) {
    platform::DWriteEngine engine;
    CheckTextEngine(engine);
}
#endif
