// The palette, held to the contrast ratios the dark mode promises.
//
// "Readable in dark mode" is not a matter of taste once the colours are
// numbers: every ink the layout paints sits on a surface the palette
// names, and the WCAG relative luminance ratio between the two is a
// build-time fact. This suite computes that ratio for every pair the
// renderers actually paint, in all three styles and both appearances,
// so a palette edit that quietly drops the body text to 3:1 fails here
// instead of shipping.
//
// The local gate builds this file together with the shared test main:
//   g++ -std=c++17 -Wall -Wextra -I src -I tests
//       tests/palette_contrast_test.cpp tests/test_main.cpp -o /tmp/palette_test

#include "gtest_lite.h"
#include "theme.h"

#include <cmath>
#include <string>

namespace {

// sRGB channel to linear light, the transfer function WCAG defines.
float LinearChannel(float value) {
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float RelativeLuminance(const ThemeColor& c) {
    return 0.2126f * LinearChannel(c.r) + 0.7152f * LinearChannel(c.g) +
           0.0722f * LinearChannel(c.b);
}

// The ratio WCAG calls contrast: (lighter + 0.05) / (darker + 0.05).
float Contrast(const ThemeColor& a, const ThemeColor& b) {
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    const float hi = la > lb ? la : lb;
    const float lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

const char* StyleName(MarkdownStyle style) {
    switch (style) {
        case MarkdownStyle::Wikipedia: return "wikipedia";
        case MarkdownStyle::Apple: return "apple";
        default: return "github";
    }
}

constexpr MarkdownStyle kStyles[] = {
    MarkdownStyle::GitHub,
    MarkdownStyle::Wikipedia,
    MarkdownStyle::Apple,
};

constexpr theme::Appearance kAppearances[] = {
    theme::Appearance::Light,
    theme::Appearance::Dark,
};

// Every ink the walk paints on the page itself.
struct InkPair {
    const char* name;
    ThemeColor ink;
    ThemeColor surface;
};

std::string Where(const char* style, const char* appearance, const char* pair) {
    return std::string(style) + "/" + appearance + "/" + pair;
}

TEST(PaletteContrast, TextClearsFourAndAHalf) {
    for (MarkdownStyle style : kStyles) {
        for (theme::Appearance appearance : kAppearances) {
            const Palette p = PaletteFor(style, appearance);
            const InkPair pairs[] = {
                // The diagram's own inks: a label sits on a node, a note
                // on its note fill, a lane title on the cluster band.
                {"diagram-node-label", p.mermaidText, p.mermaidNodeFill},
                {"diagram-note-label", p.mermaidText, p.mermaidNoteFill},
                {"diagram-actor-label", p.mermaidText, p.mermaidActorFill},
                {"diagram-lane-title", p.textPrimary, p.mermaidClusterBg},
                {"diagram-edge-label", p.textPrimary, p.mermaidLabelBg},
                {"body", p.textPrimary, p.pageBg},
                {"muted", p.textMuted, p.pageBg},
                {"heading", p.heading, p.pageBg},
                {"link", p.link, p.pageBg},
                {"quote", p.quoteText, p.pageBg},
                {"body-on-code", p.textPrimary, p.codeBg},
                {"body-on-header", p.textPrimary, p.tableHeaderBg},
                {"body-on-zebra", p.textPrimary, p.tableRowAlt},
                {"body-on-find", p.textPrimary, p.findMatchBg},
                {"body-on-find-current", p.textPrimary, p.findCurrentMatchBg},
                {"body-on-marker", p.textPrimary, p.markerBg},
                {"selected-text", p.selectionText, p.selectionBg},
                {"caret", p.caretInk, p.pageBg},
            };
            for (const InkPair& pair : pairs) {
                const float ratio = Contrast(pair.ink, pair.surface);
                const std::string where =
                    Where(StyleName(style), theme::IsDark(appearance) ? "dark" : "light",
                          pair.name);
                // The failure names the pair and the ratio, so a palette
                // edit is corrected without a second run.
                if (ratio < 4.5f) {
                    std::cerr << "    " << where << " is " << ratio << ":1\n";
                }
                ASSERT_GE(ratio, 4.5f);
            }
        }
    }
}

TEST(PaletteContrast, SyntaxColoursStayReadable) {
    // GitHub's own light orange lands at 4.44:1, so the syntax floor is
    // 4.4 rather than the body text's 4.5. The point of the check is
    // that a JSON fence never becomes decoration.
    for (MarkdownStyle style : kStyles) {
        for (theme::Appearance appearance : kAppearances) {
            const Palette p = PaletteFor(style, appearance);
            const ThemeColor kinds[] = {p.jsonKey, p.jsonString, p.jsonNumber,
                                        p.jsonLiteral};
            const char* names[] = {"key", "string", "number", "literal"};
            for (int i = 0; i < 4; ++i) {
                const float ratio = Contrast(kinds[i], p.codeBg);
                if (ratio < 4.4f) {
                    std::cerr << "    "
                              << Where(StyleName(style),
                                       theme::IsDark(appearance) ? "dark" : "light",
                                       names[i])
                              << " is " << ratio << ":1\n";
                }
                ASSERT_GE(ratio, 4.4f);
            }
        }
    }
}

TEST(PaletteContrast, SeparatorsStayVisible) {
    // A hairline is not text, and the three styles deliberately draw a
    // soft one: GitHub's own light border is 1.43:1 over white. Matching
    // the style is the higher rule, so the floor here only says "the
    // line is not invisible", and a dark border may not be weaker than
    // its light counterpart.
    for (MarkdownStyle style : kStyles) {
        const Palette light = PaletteFor(style, theme::Appearance::Light);
        const Palette dark = PaletteFor(style, theme::Appearance::Dark);
        const float lightRatio = Contrast(light.tableBorder, light.pageBg);
        const float darkRatio = Contrast(dark.tableBorder, dark.pageBg);
        ASSERT_GE(lightRatio, 1.2f);
        ASSERT_GE(darkRatio, 1.2f);
        ASSERT_GE(darkRatio, lightRatio);
        ASSERT_GE(Contrast(dark.rule, dark.pageBg), 1.2f);
        ASSERT_GE(Contrast(dark.codeBorder, dark.codeBg), 1.2f);
    }
}

TEST(PaletteContrast, DarkPaperIsDarkButNotBlack) {
    for (MarkdownStyle style : kStyles) {
        const Palette light = PaletteFor(style, theme::Appearance::Light);
        const Palette dark = PaletteFor(style, theme::Appearance::Dark);
        // Not the same page: the two appearances must differ, or the
        // toggle does nothing.
        EXPECT_GE(Contrast(light.pageBg, dark.pageBg), 4.0f);
        // Dark, by any reasonable definition: under 4:1 against white.
        EXPECT_GE(Contrast(dark.pageBg, MakeColor(0xFFFFFF)), 4.0f);
        // Not pure black: an OLED-black page is harsher than the
        // reference guide allows and hides the page boundary.
        EXPECT_GE(RelativeLuminance(dark.pageBg), 0.004f);
        // And the caret is drawn in ink, not left to the system.
        EXPECT_GE(Contrast(dark.caretInk, dark.pageBg), 4.5f);
    }
}

TEST(PaletteContrast, CalloutLabelsPassOnTheirOwnTint) {
    // The four information boxes only exist in the Apple style, the one
    // that turns calloutBoxes on.
    for (theme::Appearance appearance : kAppearances) {
        const Palette p = PaletteFor(MarkdownStyle::Apple, appearance);
        for (int kind = 0; kind < 4; ++kind) {
            ASSERT_GE(Contrast(p.calloutLabel[kind], p.calloutBg[kind]), 4.5f);
        }
    }
}

TEST(PaletteContrast, PrintStaysOnLightPaper) {
    // The print path asks BasePalette(), which means light, whatever the
    // window is wearing. Nothing in this feature may change that.
    for (MarkdownStyle style : kStyles) {
        const Palette p = BasePalette(style);
        EXPECT_GE(Contrast(p.pageBg, MakeColor(0xFFFFFF)), 1.0f);
        EXPECT_NEAR(p.pageBg.r, 1.0f, 0.001f);
        EXPECT_NEAR(p.pageBg.g, 1.0f, 0.001f);
        EXPECT_NEAR(p.pageBg.b, 1.0f, 0.001f);
    }
}

TEST(PaletteContrast, DiagramLinesStayVisible) {
    // An edge is a line, not text: the non-text floor applies. The dark
    // diagrams use mermaid's own dark theme values, so the check is that
    // a line is visible on the paper and that a cluster band reads as a
    // surface rather than the page itself.
    for (MarkdownStyle style : kStyles) {
        for (theme::Appearance appearance : kAppearances) {
            const Palette p = PaletteFor(style, appearance);
            ASSERT_GE(Contrast(p.mermaidEdge, p.pageBg), 2.0f);
            ASSERT_GE(Contrast(p.mermaidClusterBorder, p.mermaidClusterBg), 1.5f);
            ASSERT_GE(Contrast(p.mermaidClusterBg, p.pageBg), 1.01f);
        }
    }
}

TEST(PaletteContrast, DarkFlagTracksThePaper) {
    for (MarkdownStyle style : kStyles) {
        EXPECT_FALSE(PaletteFor(style, theme::Appearance::Light).dark);
        EXPECT_TRUE(PaletteFor(style, theme::Appearance::Dark).dark);
        // The SVG reader picks its light-dark() branch from this flag, so
        // it must agree with the page it is painting on.
        EXPECT_GT(RelativeLuminance(PaletteFor(style, theme::Appearance::Light).pageBg),
                  RelativeLuminance(PaletteFor(style, theme::Appearance::Dark).pageBg));
    }
}

TEST(PaletteContrast, ChromeHasADarkCounterpart) {
    const ChromePalette light = ChromePaletteFor(theme::Appearance::Light);
    const ChromePalette dark = ChromePaletteFor(theme::Appearance::Dark);
    EXPECT_GE(Contrast(light.paneText, light.paneBg), 4.5f);
    EXPECT_GE(Contrast(dark.paneText, dark.paneBg), 4.5f);
    EXPECT_GE(Contrast(light.stripBg, dark.stripBg), 4.0f);
}

}  // namespace
