#include "gtest_lite.h"

#include "../src/theme.h"
#include "../src/theme_aliases.h"

#include <string>

// The theme's alias helpers, pinned on both ports.
//
// theme.h keeps a colour and a font name in the platform's spelling, and
// the document renderer reads them through the shared helpers in
// src/theme_aliases.h. The mapping has to be the same on Windows and on
// the mac, or one port draws a different palette or asks the text engine
// for a different face. These cases name the values both ports expect, so
// a moved or rewritten helper cannot change them quietly.

TEST(ThemeAlias, Rgb24OfKeepsTheIntegerMapping) {
    EXPECT_EQ(0x000000u, theme::Rgb24Of(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(0xFFFFFFu, theme::Rgb24Of(1.0f, 1.0f, 1.0f));
    // Each channel rounds to the nearest step of 255, the way the SVG
    // transform has always encoded a palette entry.
    EXPECT_EQ(0x000000u, theme::Rgb24Of(0.0019f, 0.0f, 0.0f));
    EXPECT_EQ(0x010000u, theme::Rgb24Of(0.0020f, 0.0f, 0.0f));
    EXPECT_EQ(0x101010u,
              theme::Rgb24Of(16.0f / 255.0f, 16.0f / 255.0f,
                             16.0f / 255.0f));
}

TEST(ThemeAlias, Rgb24OfClampsAndIgnoresAlpha) {
    EXPECT_EQ(0x000000u, theme::Rgb24Of(-1.0f, -1.0f, -1.0f));
    EXPECT_EQ(0xFFFFFFu, theme::Rgb24Of(2.0f, 2.0f, 2.0f));
    // Alpha is not part of the 0xRRGGBB value; the lazy alpha of a
    // palette token must not leak into it.
    EXPECT_EQ(0x336699u, theme::Rgb24Of(0x33 / 255.0f, 0x66 / 255.0f,
                                        0x99 / 255.0f));
}

// The mac name stays a thin alias over the shared mapping, so mac
// sources that call Rgb24 are untouched.
TEST(ThemeAlias, Rgb24AliasAgreesWithTheSharedMapping) {
    const Palette palette = BasePalette(MarkdownStyle::GitHub);
    EXPECT_EQ(theme::Rgb24Of(palette.pageBg.r, palette.pageBg.g,
                             palette.pageBg.b),
              Rgb24(palette.pageBg));
    EXPECT_EQ(0xFFF6D6u, Rgb24(palette.findMatchBg));
    EXPECT_EQ(0x1F2328u, Rgb24(palette.textPrimary));
}

// A family name crosses to the seam as UTF-8 on both ports, from the
// UTF-8 string the mac theme keeps and from the wide one Windows keeps.
TEST(ThemeAlias, FamilyUtf8AnswersTheSameBytesFromBothSpellings) {
    EXPECT_TRUE(std::string("Segoe UI") == theme::FamilyUtf8("Segoe UI"));
    EXPECT_TRUE(std::string("Segoe UI") ==
                theme::FamilyUtf8(std::string("Segoe UI")));
    EXPECT_TRUE(std::string("Cascadia Mono") ==
                theme::FamilyUtf8(L"Cascadia Mono"));
    EXPECT_TRUE(std::string("Segoe UI Semibold") ==
                theme::FamilyUtf8(L"Segoe UI Semibold"));
    // An empty family is an empty answer, not a crash or a stray byte.
    EXPECT_TRUE(theme::FamilyUtf8("").empty());
    EXPECT_TRUE(theme::FamilyUtf8(static_cast<const char*>(nullptr)).empty());
    EXPECT_TRUE(theme::FamilyUtf8(static_cast<const wchar_t*>(nullptr)).empty());
}

TEST(ThemeAlias, FamilyUtf8KeepsNonAsciiNames) {
    // U+00E9, two bytes in UTF-8, from either spelling.
    EXPECT_EQ(5u, theme::FamilyUtf8("Caf\xC3\xA9").size());
    EXPECT_TRUE(std::string("Caf\xC3\xA9") ==
                theme::FamilyUtf8(L"Caf\u00E9"));
    // U+1F600 as one UTF-8 sequence of four bytes, from the wide forms
    // both platforms can hold: a 32-bit code point and a surrogate pair.
    EXPECT_TRUE(std::string("\xF0\x9F\x98\x80") ==
                theme::FamilyUtf8(L"\U0001F600"));
}

// The theme's own tokens land on the seam as the names both ports have
// always asked the text engine for.
TEST(ThemeAlias, TheThemeTokensMapToTheSeamFaces) {
    const LayoutMetrics metrics = BaseMetrics();
    EXPECT_TRUE(std::string("Segoe UI") == ThemeFamilyUtf8(metrics.bodyFont));
    EXPECT_TRUE(std::string("Cascadia Mono") ==
                ThemeFamilyUtf8(metrics.codeFont));
#ifndef _WIN32
    // The mac spells the heading weight as a family of its own; the
    // Windows build asks for the semibold weight of the body face.
    EXPECT_TRUE(std::string("Segoe UI Semibold") ==
                ThemeFamilyUtf8(THEME_BODY_FONT_SEMIBOLD));
#endif
}
