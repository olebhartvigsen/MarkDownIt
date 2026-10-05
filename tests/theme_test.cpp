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
static double DistanceFromWhite(const D2D1_COLOR_F& c) {
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