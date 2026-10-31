// The app layer's text engine, measured directly.
//
// The Core Text engine is the one piece of the mac app that the shared
// test suite cannot reach, because it lives in the app layer and talks to
// Core Text. The first real document open faulted inside its attribute
// calls, so the engine gets these tests: the measure path runs on the
// build machine from now on, over the same attributed string the app
// builds.

#include "gtest_lite.h"

#include "../src/macos/fonts_mac.h"
#include "../src/macos/seams.h"
#include "../src/macos/text_engine_mac.h"

#include <CoreText/CoreText.h>

using mac::CoreTextEngine;
using mac::FontSpec;
using mac::GlyphRun;
using mac::TextLine;

// The bundled faces still register before any test runs (the bundle
// keeps them), but the engine draws with the platform's own faces. The
// tests run from the repo root, where the vendored fonts live.
const bool kFontsRegistered = []() {
    mac::RegisterFontsFromDirectory("third_party/selawik");
    mac::RegisterFontsFromDirectory("third_party/cascadia-mono");
    return true;
}();

namespace {

GlyphRun Run(const std::string& text, bool bold = false,
             const char* family = "Segoe UI") {
    GlyphRun run;
    run.text = text;
    run.font.family = family;
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

// The registered family of a name, as Core Text answers it.
std::string FamilyOf(const char* name) {
    CFStringRef wanted = CFStringCreateWithCString(
        kCFAllocatorDefault, name, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(wanted, 13.0, nullptr);
    CFRelease(wanted);
    CFStringRef have = font ? CTFontCopyFamilyName(font) : nullptr;
    std::string out;
    if (have) {
        char text[128] = {0};
        CFStringGetCString(have, text, sizeof(text), kCFStringEncodingUTF8);
        out = text;
        CFRelease(have);
    }
    if (font) CFRelease(font);
    return out;
}

// The postscript name of the face the engine picks for a spec.
std::string PostscriptOf(const FontSpec& spec) {
    CTFontRef font = mac::CoreTextFontFor(spec);
    if (!font) return "";
    CFStringRef have = CTFontCopyPostScriptName(font);
    std::string out;
    if (have) {
        char text[128] = {0};
        CFStringGetCString(have, text, sizeof(text), kCFStringEncodingUTF8);
        out = text;
        CFRelease(have);
    }
    return out;
}

}  // namespace

TEST(TextEngineMac, UsesThePlatformFonts) {
    // The mapping the theme's family names travel through: the Windows
    // names land on the platform's own faces, the port plan's font
    // note (the system UI font for text, Menlo for code).
    EXPECT_EQ(mac::ResolvedFamily("Segoe UI", false),
              std::string("System UI"));
    EXPECT_EQ(mac::ResolvedFamily("Segoe UI Semibold", false),
              std::string("System UI"));
    EXPECT_EQ(mac::ResolvedFamily("", true), std::string("Menlo"));
    EXPECT_EQ(mac::ResolvedFamily("Helvetica Neue", false),
              std::string("Helvetica Neue"));

    // The bundled faces still answer by name, so the files stay usable.
    EXPECT_EQ(FamilyOf("Selawik"), std::string("Selawik"));
    EXPECT_EQ(FamilyOf("Cascadia Mono"), std::string("Cascadia Mono"));

    // The engine picks a real face per style from the system family:
    // regular, semibold and bold are three different faces.
    FontSpec regular;
    regular.family = "Segoe UI";
    regular.size = 16.0f;
    FontSpec semibold;
    semibold.family = "Segoe UI Semibold";
    semibold.size = 16.0f;
    FontSpec bold;
    bold.family = "Segoe UI";
    bold.size = 16.0f;
    bold.bold = true;
    const std::string r = PostscriptOf(regular);
    const std::string s = PostscriptOf(semibold);
    const std::string b = PostscriptOf(bold);
    EXPECT_FALSE(r.empty());
    EXPECT_NE(s, r);
    EXPECT_NE(b, r);
    EXPECT_NE(b, s);

    // A named bundled face still draws its own weight.
    FontSpec bundled;
    bundled.family = "Selawik";
    bundled.size = 16.0f;
    FontSpec bundledBold;
    bundledBold.family = "Selawik";
    bundledBold.size = 16.0f;
    bundledBold.bold = true;
    EXPECT_EQ(PostscriptOf(bundled), std::string("Selawik-Regular"));
    EXPECT_EQ(PostscriptOf(bundledBold), std::string("Selawik-Bold"));
}

TEST(TextEngineMac, UsesAnItalicThatIsNotTheUprightFace) {
    FontSpec regular;
    regular.family = "Segoe UI";
    regular.size = 16.0f;
    FontSpec italic;
    italic.family = "Segoe UI";
    italic.size = 16.0f;
    italic.italic = true;
    CTFontRef font = mac::CoreTextFontFor(italic);
    ASSERT_TRUE(font != nullptr);
    // The system family carries real italics: the engine asks for one
    // through the slant trait. The answer must be a distinct face, or
    // a sheared copy on a system without one; it must not be the
    // upright face unchanged.
    const CGAffineTransform matrix = CTFontGetMatrix(font);
    EXPECT_TRUE(PostscriptOf(regular) != PostscriptOf(italic) ||
                matrix.c != 0.0);
}

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
        // Every line starts at its own left edge.
        EXPECT_NEAR(line.runs[0].x, 0.0f, 0.5f);
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


TEST(TextEngineMac, KeepsTheBlankLinesOfACodeBlock) {
    // A code block is measured as one run whose text holds the hard
    // line breaks, so a blank line in the source stays a line on
    // screen instead of disappearing: the same lines the Windows
    // layout keeps for the block.
    CoreTextEngine engine;
    const std::vector<TextLine> lines =
        engine.MeasureWrapped({Run("one\n\ntwo")}, 10000.0f, 0.0f);
    ASSERT_GE(lines.size(), 3u);
    EXPECT_EQ(Rebuild(lines), std::string("one\n\ntwo"));

    int blank = 0;
    for (const TextLine& line : lines) {
        if (CoreTextEngine::LineTextUtf8(line).find_first_not_of("\r\n") ==
            std::string::npos) {
            ++blank;
        }
    }
    EXPECT_EQ(blank, 1);
}
