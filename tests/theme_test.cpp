#include "gtest_lite.h"
#include "../src/theme.h"

TEST(Theme, MermaidPaletteUsesNamedDefaults) {
    const Palette palette = BasePalette();
    EXPECT_NEAR(palette.mermaidNodeFill.r, 236.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.mermaidNodeBorder.r, 147.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.mermaidText.r, 51.0f / 255.0f, 0.001f);
}

// find-replace guide section 6: every match gets a highlight, the current
// match gets a stronger one. "Stronger" is measured here as distance from
// the white page, so the assertion is about visual weight and not about one
// channel happening to be larger in a particular hue.
static double DistanceFromWhite(const ThemeColor& c) {
    return (1.0 - c.r) + (1.0 - c.g) + (1.0 - c.b);
}

TEST(Theme, FindMatchPaletteUsesNamedDefaults) {
    const Palette palette = BasePalette();
    // 0xFFF6D6: a soft cream that stays under the text.
    EXPECT_NEAR(palette.findMatchBg.r, 255.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.findMatchBg.g, 246.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.findMatchBg.b, 214.0f / 255.0f, 0.001f);
    // 0xFFE08A: a stronger amber for the match find is standing on.
    EXPECT_NEAR(palette.findCurrentMatchBg.r, 255.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.findCurrentMatchBg.g, 224.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.findCurrentMatchBg.b, 138.0f / 255.0f, 0.001f);
}

TEST(Theme, FindCurrentMatchIsStrongerThanOtherMatches) {
    const Palette palette = BasePalette();
    const double other = DistanceFromWhite(palette.findMatchBg);
    const double current = DistanceFromWhite(palette.findCurrentMatchBg);
    EXPECT_GT(current, other);
    // "Clearly" weaker, not just a shade: the other-match fill must stay
    // below half the weight of the current-match fill.
    EXPECT_LT(other * 2.0, current);
}

TEST(Theme, FindMatchStaysBelowSelectionWeight) {
    const Palette palette = BasePalette();
    // Other matches must not compete with a real selection, and the current
    // match must not be so far below selectionBg that it reads as a different
    // kind of thing entirely.
    EXPECT_LT(DistanceFromWhite(palette.findMatchBg),
             DistanceFromWhite(palette.selectionBg));
    EXPECT_GT(DistanceFromWhite(palette.findCurrentMatchBg),
              DistanceFromWhite(palette.selectionBg) * 0.5);
}


// Port alignment plan 1.4: the marker geometry and the bullet glyphs are
// shared constants in theme.h, and both renderers read them, so the
// ports cannot drift. The test pins the values a renderer change would
// have to break on purpose.
TEST(Theme, SharedMarkerMetricsArePinned) {
    const LayoutMetrics m = BaseMetrics();
    EXPECT_NEAR(m.markerRatio, 0.6f, 1e-6f);
    EXPECT_NEAR(m.taskBoxInset, 3.0f, 1e-6f);
    EXPECT_NEAR(m.taskBoxMin, 9.0f, 1e-6f);
    EXPECT_NEAR(m.taskBoxMax, 13.0f, 1e-6f);
    EXPECT_NEAR(m.headingRulePadRatio, 0.3f, 1e-6f);
    EXPECT_NEAR(m.headingRuleThickness, 1.0f, 1e-6f);
}

TEST(Theme, MarkerGlyphsCycleByDepth) {
    // Filled bullet, hollow bullet, small square, then the cycle turns.
    const char16_t first[3] = {0x2022, 0x25E6, 0x25AA};
    for (int depth = -3; depth <= 7; ++depth) {
        const int expected = ((depth % 3) + 3) % 3;
        ASSERT_EQ(ListMarker16(depth)[0], first[expected]);
        ASSERT_EQ(static_cast<char32_t>(ListMarker32(depth)[0]),
                  static_cast<char32_t>(first[expected]));
        // Every UTF-8 marker is the glyph plus one space, and every
        // marker form names the same glyph for the same depth.
        const std::string utf8 = ListMarkerUtf8(depth);
        ASSERT_EQ(utf8.size(), static_cast<size_t>(4));
        ASSERT_EQ(ListBulletIndex(depth), expected);
    }
    std::u16string a = ListMarker16(0);
    std::u16string b = ListMarker16(3);
    EXPECT_EQ(a, b);
}

// The reading view and the page read the same hierarchy. The ratio between a
// heading and its body is one rule in theme.h, applied by both BaseMetrics and
// PaperMetrics, so a style with a large screen title cannot print one either.
TEST(Theme, ScreenHeadingsKeepThePaperRatio) {
    const float caps[7] = {0.0f, 1.8f, 1.45f, 1.2f, 1.1f, 1.0f, 1.0f};
    for (MarkdownStyle style : {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia,
                                MarkdownStyle::Apple}) {
        const LayoutMetrics screen = BaseMetrics(style);
        const LayoutMetrics paper = PaperMetrics(screen);
        for (int level = 1; level <= 6; ++level) {
            EXPECT_TRUE(screen.headingSizes[level] <=
                        screen.bodyFontSize * caps[level] + 0.001f);
            EXPECT_TRUE(paper.headingSizes[level] <=
                        paper.bodyFontSize * caps[level] + 0.001f);
            // The ratio is kept, not the absolute size: the page is smaller.
            EXPECT_NEAR(paper.headingSizes[level] / paper.bodyFontSize,
                        screen.headingSizes[level] / screen.bodyFontSize,
                        0.001f);
        }
    }
    // The Apple title the user asked to see smaller, on both surfaces.
    EXPECT_TRUE(BaseMetrics(MarkdownStyle::Apple).headingSizes[1] < 23.0f);
    EXPECT_TRUE(PaperMetrics(BaseMetrics(MarkdownStyle::Apple)).headingSizes[1]
                < 20.0f);
}

// The outline pane sits beside the page. In dark a grey pane next to a
// black page reads as two documents, so the pane takes the page colour.
TEST(Theme, DarkSidePaneTakesThePageColour) {
    const MarkdownStyle styles[] = {MarkdownStyle::GitHub, MarkdownStyle::Apple,
                                    MarkdownStyle::Wikipedia};
    for (MarkdownStyle style : styles) {
        const Palette page = PaletteFor(style, theme::Appearance::Dark);
        const ChromePalette pane =
            SidePanePaletteFor(theme::Appearance::Dark, style);
        EXPECT_NEAR(pane.paneBg.r, page.pageBg.r, 0.001f);
        EXPECT_NEAR(pane.paneBg.g, page.pageBg.g, 0.001f);
        EXPECT_NEAR(pane.paneBg.b, page.pageBg.b, 0.001f);
        // Opaque: the pane must cover the sidebar material behind it.
        EXPECT_NEAR(pane.paneBg.a, 1.0f, 0.001f);
    }
}

// In light nothing changes: the warm chrome grey stays.
TEST(Theme, LightSidePaneKeepsTheChromeColour) {
    const ChromePalette chrome = ChromePaletteFor(theme::Appearance::Light);
    const ChromePalette pane =
        SidePanePaletteFor(theme::Appearance::Light, MarkdownStyle::GitHub);
    EXPECT_NEAR(pane.paneBg.r, chrome.paneBg.r, 0.001f);
    EXPECT_NEAR(pane.paneBg.g, chrome.paneBg.g, 0.001f);
    EXPECT_NEAR(pane.paneBg.b, chrome.paneBg.b, 0.001f);
}
