#include "gtest_lite.h"
#include "../src/theme.h"

TEST(Theme, MermaidPaletteUsesNamedDefaults) {
    const Palette palette = BasePalette();
    EXPECT_NEAR(palette.mermaidNodeFill.r, 236.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.mermaidNodeBorder.r, 147.0f / 255.0f, 0.001f);
    EXPECT_NEAR(palette.mermaidText.r, 51.0f / 255.0f, 0.001f);
}