#pragma once

#include <d2d1.h>

// All layout spacing, font sizes, and colors live here as named tokens.
// Both Measure and Render read from LayoutMetrics so the two passes
// can never drift apart. Create one via ComputeMetrics(zoom).

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
    const wchar_t* bodyFont;
    const wchar_t* codeFont;
};

struct Palette {
    D2D1_COLOR_F textPrimary;
    D2D1_COLOR_F textMuted;
    D2D1_COLOR_F heading;
    D2D1_COLOR_F link;
    D2D1_COLOR_F codeBg;
    D2D1_COLOR_F codeBorder;
    D2D1_COLOR_F inlineCodeBg;
    D2D1_COLOR_F quoteBar;
    D2D1_COLOR_F quoteText;
    D2D1_COLOR_F rule;
    D2D1_COLOR_F tableBorder;
    D2D1_COLOR_F tableHeaderBg;
    D2D1_COLOR_F tableRowAlt;
};

inline LayoutMetrics BaseMetrics() {
    LayoutMetrics m = {};
    m.padX = 56.0f;
    m.padTop = 32.0f;
    m.maxContentWidth = 720.0f;
    m.bodyLineHeight = 1.55f;
    m.headingLineHeight = 1.25f;
    m.codeLineHeight = 1.45f;
    m.paraGap = 16.0f;
    m.headingGapBefore = 32.0f;
    m.headingGapBeforeMinor = 24.0f;
    m.headingGapAfter = 10.0f;
    m.listItemGap = 6.0f;
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
    m.bodyFontSize = 15.0f;
    m.codeFontSize = 12.5f;
    m.headingSizes[0] = 0.0f;
    m.headingSizes[1] = 26.0f;
    m.headingSizes[2] = 21.0f;
    m.headingSizes[3] = 17.0f;
    m.headingSizes[4] = 15.0f;
    m.headingSizes[5] = 13.5f;
    m.headingSizes[6] = 13.0f;
    m.bodyFont = L"Segoe UI";
    m.codeFont = L"Cascadia Mono";
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
    p.textPrimary = D2D1::ColorF(0x24292F);
    p.textMuted = D2D1::ColorF(0x656D76);
    p.heading = D2D1::ColorF(0x1F2328);
    p.link = D2D1::ColorF(0x0969DA);
    p.codeBg = D2D1::ColorF(0xF6F8FA);
    p.codeBorder = D2D1::ColorF(0xD0D7DE);
    p.inlineCodeBg = D2D1::ColorF(0xEFF1F3);
    p.quoteBar = D2D1::ColorF(0xD0D7DE);
    p.quoteText = D2D1::ColorF(0x656D76);
    p.rule = D2D1::ColorF(0xD8DEE4);
    p.tableBorder = D2D1::ColorF(0xD0D7DE);
    p.tableHeaderBg = D2D1::ColorF(0xF6F8FA);
    p.tableRowAlt = D2D1::ColorF(0xFAFBFC);
    return p;
}
