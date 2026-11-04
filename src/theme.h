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
// faces. The mac engine draws these names with the platform's own
// system faces and keeps the bundled metric compatible stand-ins
// (Selawik for Segoe UI, Cascadia Mono) for a comparison or a future
// setting, so a document can still be laid out the same way on both
// systems.
#ifdef _WIN32
#define THEME_BODY_FONT L"Segoe UI"
#define THEME_CODE_FONT L"Cascadia Mono"
#else
#define THEME_BODY_FONT "Segoe UI"
// The family the heading runs ask for. The Windows build draws
// headings with the semibold weight of the body face; the mac
// build spells that intent as this name, which its engine maps
// onto the system face's semibold weight.
#define THEME_BODY_FONT_SEMIBOLD "Segoe UI Semibold"
#define THEME_CODE_FONT "Cascadia Mono"
#endif

struct LayoutMetrics {
    // Side gutter and top padding.
    float padX;
    float padTop;

    // Windows width cap. The mac renderer deliberately ignores it and
    // follows the pane; paragraphs wider than this are centered and
    // capped only on Windows.
    float maxContentWidth;

    // Line height multipliers (relative to font size).
    float bodyLineHeight;
    float headingLineHeight;
    float codeLineHeight;

    // Block spacing.
    float paraGap;
    float headingGapBefore;       // above any heading
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
    // GitHub's markdown scale, in em multiples of the 16px body.
    // The line heights are the CSS line-height values, not a
    // multiplier of the font's natural box: 16px body at 1.5, a
    // heading at 1.25 and a code line at 1.45.
    m.bodyLineHeight = 1.5f;
    m.headingLineHeight = 1.25f;
    m.codeLineHeight = 1.45f;
    // GitHub's collapsed vertical margins: a paragraph keeps 1rem,
    // a heading 1.5rem above and 1rem below, a list, a quote, a
    // table and a fenced block 1rem below and nothing above, and a
    // thematic break 1.5rem on each side.
    m.paraGap = 16.0f;
    m.headingGapBefore = 24.0f;
    m.headingGapAfter = 16.0f;
    m.listItemGap = 4.0f;
    m.listGap = 16.0f;
    m.codeBlockGap = 16.0f;
    m.ruleGapAbove = 24.0f;
    m.ruleGapBelow = 24.0f;
    // The fenced block's 1rem padding and 6px corner radius.
    m.codePad = 16.0f;
    m.codeRadius = 6.0f;
    // .25em bar plus 1em of padding at the body size.
    m.quoteIndent = 20.0f;
    m.quoteBarWidth = 4.0f;
    // GitHub's list padding is 2em to the text. The renderer puts
    // the marker at 1.25em and reserves a 1em marker box inside the
    // indent, so the text edge lands at 2.25em, four DIP wider than
    // GitHub's 2em. The extra gutter is what keeps a wrapped line
    // clear of the bullet.
    m.listIndent = 20.0f;
    m.markerGutter = 4.0f;
    // GitHub's table cells: 6px above and below, 13px at the sides.
    m.cellPadX = 13.0f;
    m.cellPadY = 6.0f;
    // GitHub's type scale in points: 16px body and 13.6px code at
    // 96/72, and the 2 / 1.5 / 1.25 / 1 / .875 / .85 em headings.
    m.bodyFontSize = 12.0f;
    m.codeFontSize = 10.2f;  // 13.6 px, GitHub's 85% code size
    m.headingSizes[0] = 0.0f;
    m.headingSizes[1] = 24.0f;
    m.headingSizes[2] = 18.0f;
    m.headingSizes[3] = 15.0f;
    m.headingSizes[4] = 12.0f;
    m.headingSizes[5] = 10.5f;
    m.headingSizes[6] = 10.2f;
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
    // GitHub's current markdown palette (light): the default ink, the
    // muted ink its quote and H6 rules use, the accent link, the muted
    // code and table surface, and the border colour that draws the
    // tables, the code cards and the heading rules.
    p.textPrimary = MakeColor(0x1F2328);
    p.textMuted = MakeColor(0x59636E);
    p.heading = MakeColor(0x1F2328);
    p.link = MakeColor(0x0969DA);
    p.codeBg = MakeColor(0xF6F8FA);
    p.codeBorder = MakeColor(0xD1D9E0);
    p.mermaidNodeFill = MakeColor(0xECECFF);
    p.mermaidNodeBorder = MakeColor(0x9370DB);
    p.mermaidText = MakeColor(0x333333);
    p.inlineCodeBg = MakeColor(0xEFF1F3);
    p.quoteBar = MakeColor(0xD1D9E0);
    p.quoteText = MakeColor(0x59636E);
    p.rule = MakeColor(0xD1D9E0);
    p.tableBorder = MakeColor(0xD1D9E0);
    // A markdown table's header row is the page's own paper; the
    // border and the semibold weight carry the header.
    p.tableHeaderBg = MakeColor(0xFFFFFF);
    p.tableRowAlt = MakeColor(0xF6F8FA);
    p.selectionBg = MakeColor(0xB4D5FE);
    p.selectionText = MakeColor(0x000000);
    p.findMatchBg = MakeColor(0xFFF6D6);
    p.findCurrentMatchBg = MakeColor(0xFFE08A);
    p.markerBg = MakeColor(0xFFD54A);
    p.markerLine = MakeColor(0xB8860B);
    return p;
}

// The chrome palette: the surfaces the app draws around the document,
// the outline pane and the command strip. One source for both builds,
// so the two platforms keep the same look. The light values are the
// ones the Windows build has always painted; the strip follows the
// Windows ribbon's own theme (its light grey panel, the blue File tab
// and the pale blue pressed pills).
struct ChromePalette {
    ThemeColor paneBg;
    ThemeColor paneSep;
    ThemeColor paneText;
    ThemeColor paneTextMuted;
    ThemeColor paneActiveBg;
    ThemeColor paneActiveText;
    ThemeColor paneHoverBg;
    ThemeColor paneArrow;
    ThemeColor stripBg;
    ThemeColor stripLine;
    ThemeColor stripLineSoft;
    ThemeColor groupLabel;
    ThemeColor tabFileBg;
    ThemeColor tabFileText;
    ThemeColor tabActiveBg;
    ThemeColor tabActiveText;
    ThemeColor tabInactiveText;
    ThemeColor pillBg;
    ThemeColor pillBorder;
};

inline ChromePalette BaseChromePalette() {
    ChromePalette c = {};
    c.paneBg = MakeColor(0xFAF6F1);
    c.paneSep = MakeColor(0xE0D6CC);
    c.paneText = MakeColor(0x3A342C);
    c.paneTextMuted = MakeColor(0x8A8072);
    c.paneActiveBg = MakeColor(0xE8DCC8);
    c.paneActiveText = MakeColor(0x2A2118);
    c.paneHoverBg = MakeColor(0xF0E9DF);
    c.paneArrow = MakeColor(0x6B6154);
    c.stripBg = MakeColor(0xF5F6F7);
    c.stripLine = MakeColor(0xDADBDC);
    c.stripLineSoft = MakeColor(0xE3E4E5);
    c.groupLabel = MakeColor(0x5A5A5A);
    c.tabFileBg = MakeColor(0x1979CA);
    c.tabFileText = MakeColor(0xFFFFFF);
    c.tabActiveBg = MakeColor(0xFFFFFF);
    // Both tab captions are dark; the card behind the active tab is
    // what marks it, the way the ribbon does it.
    c.tabActiveText = MakeColor(0x3C3C3C);
    c.tabInactiveText = MakeColor(0x333333);
    c.pillBg = MakeColor(0xC9E0F7);
    c.pillBorder = MakeColor(0x62A2E4);
    return c;
}

// The dark counterparts. The Windows build is light only, so these are
// the mac's warm-dark neutrals: the same shapes and accents, kept
// readable in Dark Mode.
inline ChromePalette DarkChromePalette() {
    ChromePalette c = {};
    c.paneBg = MakeColor(0x2A2622);
    c.paneSep = MakeColor(0x494139);
    c.paneText = MakeColor(0xE6DED2);
    c.paneTextMuted = MakeColor(0x9C9083);
    c.paneActiveBg = MakeColor(0x4A3E2E);
    c.paneActiveText = MakeColor(0xFFF3E2);
    c.paneHoverBg = MakeColor(0x363029);
    c.paneArrow = MakeColor(0xB8AC9C);
    c.stripBg = MakeColor(0x2B2B2B);
    c.stripLine = MakeColor(0x3D3D3D);
    c.stripLineSoft = MakeColor(0x383838);
    c.groupLabel = MakeColor(0x9A9A9A);
    c.tabFileBg = MakeColor(0x1979CA);
    c.tabFileText = MakeColor(0xFFFFFF);
    c.tabActiveBg = MakeColor(0x3A3A3A);
    c.tabActiveText = MakeColor(0x9DB0D0);
    c.tabInactiveText = MakeColor(0xD8D8D8);
    c.pillBg = MakeColor(0x2F4A66);
    c.pillBorder = MakeColor(0x4A86C8);
    return c;
}
