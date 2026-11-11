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

#include "markdown_style.h"

#ifdef _WIN32
#include <d2d1.h>
#include <string>
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
    float markerRatio;   // marker x = listIndent * this + markerGutter

    // Task checkbox edge, in the already-scaled paint space: the paint
    // pass derives it from the marker width, so it does not ride the
    // zoom scaling in ScaleMetrics.
    float taskBoxInset;  // space between box edge and the marker edge
    float taskBoxMin;    // smallest drawn edge
    float taskBoxMax;    // largest drawn edge

    // Heading rule (the line under H1 and H2). The pad ratio rides the
    // heading font size; the thickness is zoomed at the paint site.
    float headingRulePadRatio;
    float headingRuleThickness;

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

    // Style switches. The shared layout reads these instead of
    // assuming one style, so a profile in BaseMetrics(style) is the
    // whole difference between two looks.
    //
    // Heading face per level, UTF-8 (index 0 unused). nullptr means
    // the semibold body face; a name takes that family, with
    // headingBold[level] choosing its weight.
    const char* headingFamily[7];
    bool headingBold[7];
    bool linkUnderline;          // links underline at rest
    float thematicBreakThickness;  // the <hr> bar, DIP at zoom 1
    float codeBorderWidth;       // 0 draws the fenced card borderless
    bool tableHeaderFill;        // header row takes palette.tableHeaderBg
    bool tableZebra;             // every second body row takes tableRowAlt
    bool tableBodyFill;          // every body row takes tableRowAlt
    bool tableHeaderCenter;      // header cells without a mark center
};

// The unordered list marker glyphs, one table for both renderers so
// the cycling cannot drift between the ports: a filled bullet, a
// hollow bullet, a small square. U+2022, U+25E6, U+25AA.
inline constexpr char16_t kListBulletGlyphs[3] = {
    u'\u2022', u'\u25E6', u'\u25AA',
};

inline int ListBulletIndex(int depth) { return ((depth % 3) + 3) % 3; }

// The marker the Windows paint pass draws, UTF-16 for DirectWrite.
inline std::u16string ListMarker16(int depth) {
    std::u16string s;
    s += kListBulletGlyphs[ListBulletIndex(depth)];
    s += u' ';
    return s;
}

// The same marker for the Windows measure pass, UTF-32, which reserves
// one extra space after the glyph.
inline std::u32string ListMarker32(int depth) {
    std::u32string s;
    s += static_cast<char32_t>(kListBulletGlyphs[ListBulletIndex(depth)]);
    s += U' ';
    s += U' ';
    return s;
}

// The same marker for the mac paint pass, UTF-8 for Core Text. Every
// glyph sits in the three-byte UTF-8 range, so no wide encoder is
// needed.
inline std::string ListMarkerUtf8(int depth) {
    std::string s;
    const char16_t cp = kListBulletGlyphs[ListBulletIndex(depth)];
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
    s += ' ';
    return s;
}

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

inline LayoutMetrics GitHubMetrics() {
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
    // The marker sits 0.6 of the indent in from the text edge, and the
    // task checkbox runs 9 to 13 DIP centred on the first line.
    m.markerRatio = 0.6f;
    m.taskBoxInset = 3.0f;
    m.taskBoxMin = 9.0f;
    m.taskBoxMax = 13.0f;
    // The heading rule pads 0.3 of the heading size above it and rides
    // a 1 DIP stroke.
    m.headingRulePadRatio = 0.3f;
    m.headingRuleThickness = 1.0f;
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
    // Headings are the semibold body face at every level.
    for (int i = 0; i < 7; ++i) {
        m.headingFamily[i] = nullptr;
        m.headingBold[i] = false;
    }
    m.linkUnderline = true;
    m.thematicBreakThickness = 4.0f;  // GitHub's .25em bar
    m.codeBorderWidth = 0.0f;
    m.tableHeaderFill = false;
    m.tableZebra = true;
    m.tableBodyFill = false;
    m.tableHeaderCenter = false;
    return m;
}

// The Wikipedia article look. The numbers come from the Vector skin's
// own stylesheet (skins.vector.styles.legacy, mediawiki.skinning), not
// from guesswork: a 14px body at line-height 1.6, h1 1.8em and h2
// 1.5em in the serif stack at normal weight with a 1px rule under
// them, h3 to h6 bold in the sans face, paragraphs 0.5em apart,
// lists indented 1.6em with 0.1em between items, a .wikitable cell
// of 0.2em by 0.4em, a quote with a 4px bar and 32px of padding.
// Points are px times 0.75. The Manual of Style itself is editorial,
// so what it adds here is the structure it asks for: a real heading
// hierarchy, quotations set off by indentation instead of marks, and
// colour never the only carrier of meaning (the link keeps its
// distinct hue and the table header keeps its bold weight).
inline LayoutMetrics WikipediaMetrics() {
    LayoutMetrics m = GitHubMetrics();
    m.bodyLineHeight = 1.6f;
    m.headingLineHeight = 1.375f;
    m.codeLineHeight = 1.3f;
    m.paraGap = 7.0f;
    m.headingGapBefore = 18.0f;
    m.headingGapAfter = 5.0f;
    m.listItemGap = 1.4f;
    m.listGap = 7.0f;
    m.codeBlockGap = 10.0f;
    m.ruleGapAbove = 7.0f;
    m.ruleGapBelow = 7.0f;
    m.codePad = 14.0f;
    m.codeRadius = 2.0f;
    m.codeBorderWidth = 1.0f;
    m.quoteIndent = 32.0f;
    m.quoteBarWidth = 4.0f;
    m.listIndent = 14.0f;
    m.markerGutter = 3.0f;
    m.headingRulePadRatio = 0.05f;
    m.headingRuleThickness = 1.0f;
    m.cellPadX = 5.6f;
    m.cellPadY = 2.8f;
    m.bodyFontSize = 10.5f;   // 14 px
    m.codeFontSize = 9.75f;   // 13 px, the browser's monospace rule
    m.headingSizes[1] = 18.9f;   // 1.8em
    m.headingSizes[2] = 15.75f;  // 1.5em
    m.headingSizes[3] = 12.6f;   // 1.2em
    m.headingSizes[4] = 10.5f;
    m.headingSizes[5] = 10.5f;
    m.headingSizes[6] = 10.5f;
    m.headingFamily[1] = "Georgia";
    m.headingFamily[2] = "Georgia";
    for (int i = 3; i <= 6; ++i) {
        m.headingFamily[i] = "Segoe UI";
        m.headingBold[i] = true;
    }
    m.linkUnderline = false;
    m.thematicBreakThickness = 1.0f;
    m.tableHeaderFill = true;
    m.tableZebra = false;
    m.tableBodyFill = true;
    m.tableHeaderCenter = true;  // .wikitable th { text-align: center }
    return m;
}

inline LayoutMetrics BaseMetrics(MarkdownStyle style = ActiveMarkdownStyle()) {
    return style == MarkdownStyle::Wikipedia ? WikipediaMetrics()
                                             : GitHubMetrics();
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
    m.codeBorderWidth *= zoom;
    m.listIndent *= zoom;
    m.markerGutter *= zoom;
    // The remaining list constants are ratios, or already live in the
    // scaled paint space where the renderers apply their own zoom, so
    // they hold their value here.
    m.cellPadX *= zoom;
    m.cellPadY *= zoom;
    // Line height multipliers are unitless ratios, not DIPs.
    // Font sizes are in points and are multiplied by zoom elsewhere
    // (Init applies zoom_ during CreateTextFormat).
    return m;
}

inline Palette GitHubPalette() {
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

// Wikipedia's article colours (Vector light): base ink 202122, subtle
// ink 54595d, link 3366cc, the neutral surface f8f9fa and its darker
// step eaecf0, and the base border a2a9b1 that draws the heading
// rules, the thematic break and the wikitable grid. The editor's own
// selection, find and marker colours stay as the GitHub palette has
// them, because they are app chrome and not article style.
inline Palette WikipediaPalette() {
    Palette p = GitHubPalette();
    p.textPrimary = MakeColor(0x202122);
    p.textMuted = MakeColor(0x54595D);
    p.heading = MakeColor(0x101418);
    p.link = MakeColor(0x3366CC);
    p.codeBg = MakeColor(0xF8F9FA);
    p.codeBorder = MakeColor(0xEAECF0);
    p.inlineCodeBg = MakeColor(0xF8F9FA);
    p.quoteBar = MakeColor(0xEAECF0);
    p.quoteText = MakeColor(0x202122);  // a Wikipedia quote is body ink
    p.rule = MakeColor(0xA2A9B1);
    p.tableBorder = MakeColor(0xA2A9B1);
    p.tableHeaderBg = MakeColor(0xEAECF0);
    p.tableRowAlt = MakeColor(0xF8F9FA);
    return p;
}

inline Palette BasePalette(MarkdownStyle style = ActiveMarkdownStyle()) {
    return style == MarkdownStyle::Wikipedia ? WikipediaPalette()
                                             : GitHubPalette();
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
