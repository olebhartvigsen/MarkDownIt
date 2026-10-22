#pragma once

// All layout spacing, font sizes, and colors live here as named tokens.
// Both Measure and Render read from LayoutMetrics so the two passes
// can never drift apart. Create one via ComputeMetrics(zoom).
//
// The file is shared between the Windows and the macOS build. The
// Windows build hands the palette straight to Direct2D and the font
// names straight to DirectWrite, so on Windows ThemeColor is
// D2D1_COLOR_F and ThemeFontName is a wide string. Elsewhere the same
// names refer to a plain 4-float RGBA struct and a UTF-8 string, so no
// macOS translation unit has to include <d2d1.h>.

#ifdef _WIN32
#include <d2d1.h>
#else
#include <cstdint>
#include <string>
#endif

// A colour in the palette.
//
// On Windows this is D2D1_COLOR_F, so a palette entry can be passed
// straight to ID2D1RenderTarget::CreateSolidColorBrush. Everywhere else
// it is the same four floats under the same names.
#ifdef _WIN32
using ThemeColor = D2D1_COLOR_F;
#else
struct ThemeColor {
    float r;
    float g;
    float b;
    float a;
};
#endif

// A font family name.
//
// On Windows this is a wide string, which is what
// IDWriteFactory::CreateTextFormat expects. Everywhere else it is a
// UTF-8 string, which the Core Text layer converts when it builds a
// CTFont.
#ifdef _WIN32
using ThemeFontName = const wchar_t*;
#else
using ThemeFontName = std::string;
#endif

// Build a colour from a 0xRRGGBB value with the given alpha.
inline ThemeColor MakeColor(uint32_t rgb, float alpha = 1.0f) {
#ifdef _WIN32
    return D2D1::ColorF(rgb, alpha);
#else
    ThemeColor c;
    c.r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    c.b = static_cast<float>(rgb & 0xFF) / 255.0f;
    c.a = alpha;
    return c;
#endif
}

// The default font family names. Both platforms ask for the same
// faces. The mac build bundles metric compatible copies (Selawik for
// Segoe UI, and Cascadia Mono itself) and maps these names onto them in
// its text engine, so a document lays out the same way on both systems.
#ifdef _WIN32
#define THEME_BODY_FONT L"Segoe UI"
#define THEME_CODE_FONT L"Cascadia Mono"
#else
#define THEME_BODY_FONT "Segoe UI"
#define THEME_CODE_FONT "Cascadia Mono"
#endif

struct LayoutMetrics {
    // Side gutter and top padding.
    float padX;
    float padTop;

    // Maximum content width for readable line length. Paragraphs
    // wider than this are centered and capped.
    float maxContentWidth;

    // Line height multipliers (relative to font size).
    float bodyLineHeight;
    float headingLineHeight;
    float codeLineHeight;

    // Block spacing.
    float paraGap;
    float headingGapBefore;       // above H1, H2
    float headingGapBeforeMinor;  // above H3-H6
    float headingGapAfter;        // below any heading
    float listItemGap;            // between items in one list
    float listGap;                // around a whole list
    float codeBlockGap;           // around a fenced block
    float ruleGapAbove;           // HR line under H1/H2
    float ruleGapBelow;           // space below the HR line

    // Code block.
    float codePad;
    float codeRadius;

    // Blockquote.
    float quoteIndent;   // per depth level
    float quoteBarWidth;

    // List.
    float listIndent;    // per depth level
    float markerGutter;  // extra space after marker

    // Table.
    float cellPadX;
    float cellPadY;

    // Font sizes in points (pre-zoom, pre-DIP conversion).
    float bodyFontSize;
    float codeFontSize;
    float headingSizes[7];  // index 0 unused

    // Font family names.
    ThemeFontName bodyFont;
    ThemeFontName codeFont;
};

struct Palette {
    ThemeColor textPrimary;
    ThemeColor textMuted;
    ThemeColor heading;
    ThemeColor link;
    ThemeColor codeBg;
    ThemeColor codeBorder;
    ThemeColor mermaidNodeFill;
    ThemeColor mermaidNodeBorder;
    ThemeColor mermaidText;
    ThemeColor inlineCodeBg;
    ThemeColor quoteBar;
    ThemeColor quoteText;
    ThemeColor rule;
    ThemeColor tableBorder;
    ThemeColor tableHeaderBg;
    ThemeColor tableRowAlt;
    ThemeColor selectionBg;
    ThemeColor selectionText;
    // Search-result highlight fills. findMatchBg marks every other match and
    // stays clearly weaker than selectionBg so a long result list does not
    // compete with the text. findCurrentMatchBg marks the match find is
    // standing on and carries close to selectionBg weight, so "Enter jumps
    // here" is visible at a glance.
    ThemeColor findMatchBg;
    ThemeColor findCurrentMatchBg;
    // Marker annotation layer: a warm highlighter fill drawn behind the
    // text plus a darker line along the bottom edge of each marked range,
    // so a mark never relies on colour alone.
    ThemeColor markerBg;
    ThemeColor markerLine;
};

inline LayoutMetrics BaseMetrics() {
    LayoutMetrics m = {};
    m.padX = 56.0f;
    m.padTop = 32.0f;
    m.maxContentWidth = 800.0f;
    m.bodyLineHeight = 1.3f;
    m.headingLineHeight = 1.25f;
    m.codeLineHeight = 1.45f;
    m.paraGap = 16.0f;
    m.headingGapBefore = 32.0f;
    m.headingGapBeforeMinor = 24.0f;
    m.headingGapAfter = 10.0f;
    m.listItemGap = 4.0f;
    m.listGap = 16.0f;
    m.codeBlockGap = 18.0f;
    m.ruleGapAbove = 8.0f;
    m.ruleGapBelow = 20.0f;
    m.codePad = 14.0f;
    m.codeRadius = 6.0f;
    m.quoteIndent = 20.0f;
    m.quoteBarWidth = 4.0f;
    m.listIndent = 28.0f;
    m.markerGutter = 4.0f;
    m.cellPadX = 12.0f;
    m.cellPadY = 8.0f;
    m.bodyFontSize = 13.5f;
    m.codeFontSize = 11.5f;
    m.headingSizes[0] = 0.0f;
    m.headingSizes[1] = 23.0f;
    m.headingSizes[2] = 19.0f;
    m.headingSizes[3] = 15.5f;
    m.headingSizes[4] = 13.5f;
    m.headingSizes[5] = 12.5f;
    m.headingSizes[6] = 12.0f;
    m.bodyFont = THEME_BODY_FONT;
    m.codeFont = THEME_CODE_FONT;
    return m;
}

inline LayoutMetrics ScaleMetrics(const LayoutMetrics& base, float zoom) {
    LayoutMetrics m = base;
    m.padX *= zoom;
    m.padTop *= zoom;
    m.maxContentWidth *= zoom;
    m.paraGap *= zoom;
    m.headingGapBefore *= zoom;
    m.headingGapBeforeMinor *= zoom;
    m.headingGapAfter *= zoom;
    m.listItemGap *= zoom;
    m.listGap *= zoom;
    m.codeBlockGap *= zoom;
    m.ruleGapAbove *= zoom;
    m.ruleGapBelow *= zoom;
    m.codePad *= zoom;
    m.codeRadius *= zoom;
    m.quoteIndent *= zoom;
    m.quoteBarWidth *= zoom;
    m.listIndent *= zoom;
    m.markerGutter *= zoom;
    m.cellPadX *= zoom;
    m.cellPadY *= zoom;
    // Line height multipliers are unitless ratios, not DIPs.
    // Font sizes are in points and are multiplied by zoom elsewhere
    // (Init applies zoom_ during CreateTextFormat).
    return m;
}

inline Palette BasePalette() {
    Palette p = {};
    p.textPrimary = MakeColor(0x24292F);
    p.textMuted = MakeColor(0x656D76);
    p.heading = MakeColor(0x1F2328);
    p.link = MakeColor(0x0969DA);
    p.codeBg = MakeColor(0xF6F8FA);
    p.codeBorder = MakeColor(0xD0D7DE);
    p.mermaidNodeFill = MakeColor(0xECECFF);
    p.mermaidNodeBorder = MakeColor(0x9370DB);
    p.mermaidText = MakeColor(0x333333);
    p.inlineCodeBg = MakeColor(0xEFF1F3);
    p.quoteBar = MakeColor(0xD0D7DE);
    p.quoteText = MakeColor(0x656D76);
    p.rule = MakeColor(0xD8DEE4);
    p.tableBorder = MakeColor(0xD0D7DE);
    p.tableHeaderBg = MakeColor(0xF6F8FA);
    p.tableRowAlt = MakeColor(0xFAFBFC);
    p.selectionBg = MakeColor(0xB4D5FE);
    p.selectionText = MakeColor(0x000000);
    p.findMatchBg = MakeColor(0xFFF6D6);
    p.findCurrentMatchBg = MakeColor(0xFFE08A);
    p.markerBg = MakeColor(0xFFD54A);
    p.markerLine = MakeColor(0xB8860B);
    return p;
}
