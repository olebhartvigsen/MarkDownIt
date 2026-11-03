// The mac palette: the theme's colour slots, filled from AppKit's
// semantic colours instead of fixed numbers.
//
// The visual style guide asks for the system's own colours, so the
// document follows Light Mode and Dark Mode and the user's contrast
// settings the way the rest of the system does. The slots keep their
// theme names, so the renderer works from the same struct it always
// did. The mermaid diagram colours stay mermaid's own default theme on
// purpose: the diagram's identity is mermaid's, not the app's, the way
// a rendered diagram keeps its colours on the web.

#import <AppKit/AppKit.h>

#include "system_theme_mac.h"

namespace mac {
namespace {

// One semantic colour as the palette's four floats, resolved through
// sRGB so the numbers mean what the renderer expects.
ThemeColor FromColor(NSColor* color) {
    NSColor* rgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!rgb) rgb = color;
    CGFloat r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    @try {
        [rgb getRed:&r green:&g blue:&b alpha:&a];
    } @catch (NSException* e) {
        r = 0.0;
        g = 0.0;
        b = 0.0;
        a = 1.0;
    }
    ThemeColor c;
    c.r = static_cast<float>(r);
    c.g = static_cast<float>(g);
    c.b = static_cast<float>(b);
    c.a = static_cast<float>(a);
    return c;
}

// The same colour at a weaker alpha: the subtle fills (a table row, a
// highlight) want a wash of a semantic colour, not a solid one.
ThemeColor Faded(NSColor* color, CGFloat alpha) {
    return FromColor([color colorWithAlphaComponent:alpha]);
}

}  // namespace

Palette SystemPalette() {
    Palette p = BasePalette();
    p.textPrimary = FromColor([NSColor labelColor]);
    // The system's own secondary label measures about 3.3 to 1 over
    // white, below the guideline's 4.5 to 1 for muted text, so the
    // muted slot is the label colour at a gentler alpha instead;
    // it still follows both appearances.
    p.textMuted = Faded([NSColor labelColor], 0.62);
    p.heading = FromColor([NSColor labelColor]);
    p.link = FromColor([NSColor linkColor]);
    // A code card must not be the paper it sits on: in the light
    // appearance windowBackgroundColor is the same white, so the
    // fenced block had no visible surface. The quaternary label wash
    // is the same semantic grey the inline spans already use.
    p.codeBg = FromColor([NSColor quaternaryLabelColor]);
    p.codeBorder = FromColor([NSColor separatorColor]);
    // p.mermaidNodeFill / mermaidNodeBorder / mermaidText stay the base
    // values: the diagrams keep mermaid's default theme.
    p.inlineCodeBg = FromColor([NSColor quaternaryLabelColor]);
    p.quoteBar = FromColor([NSColor separatorColor]);
    // The same muted slot as the body text, for the same contrast
    // reason.
    p.quoteText = Faded([NSColor labelColor], 0.62);
    p.rule = FromColor([NSColor separatorColor]);
    p.tableBorder = FromColor([NSColor separatorColor]);
    p.tableHeaderBg = FromColor([NSColor quaternaryLabelColor]);
    // Half the header's weight: setting a label colour's alpha means
    // its own strength, and 0.55 read darker than the header, not
    // fainter.
    p.tableRowAlt = Faded([NSColor quaternaryLabelColor], 0.05);
    p.selectionBg = FromColor([NSColor selectedContentBackgroundColor]);
    p.selectionText = FromColor([NSColor selectedTextColor]);
    p.findMatchBg = Faded([NSColor systemYellowColor], 0.30);
    p.findCurrentMatchBg = Faded([NSColor systemYellowColor], 0.62);
    p.markerBg = Faded([NSColor systemYellowColor], 0.45);
    p.markerLine = Faded([NSColor systemOrangeColor], 0.85);
    return p;
}

// The chrome palette for the current appearance: the shared fixed
// values in Light Mode, the warm-dark counterparts in Dark Mode.
ChromePalette ChromePaletteForAppearance(NSAppearance* appearance) {
    NSAppearanceName best = [(appearance ? appearance : NSApp.effectiveAppearance)
        bestMatchFromAppearancesWithNames:@[
            NSAppearanceNameAqua, NSAppearanceNameDarkAqua
        ]];
    if ([best isEqualToString:NSAppearanceNameDarkAqua]) {
        return DarkChromePalette();
    }
    return BaseChromePalette();
}

// The colours of the sidebar, resolved from the system's own
// semantic colours under the asked appearance, so the pane follows
// Light Mode, Dark Mode, the user's accent colour and the contrast
// settings the way the rest of the system does.
ChromePalette SidebarPaletteForAppearance(NSAppearance* appearance) {
    ChromePalette c = {};
    __block ThemeColor text = {};
    __block ThemeColor muted = {};
    __block ThemeColor activeBg = {};
    __block ThemeColor activeText = {};
    void (^resolve)(void) = ^{
        text = FromColor([NSColor labelColor]);
        muted = FromColor([NSColor secondaryLabelColor]);
        activeBg = FromColor([NSColor selectedContentBackgroundColor]);
        activeText = FromColor([NSColor alternateSelectedControlTextColor]);
    };
    if (appearance) {
        [appearance performAsCurrentDrawingAppearance:resolve];
    } else {
        resolve();
    }
    // The material below the pane is the backdrop; the pane itself
    // stays clear.
    c.paneBg = ThemeColor{0.0f, 0.0f, 0.0f, 0.0f};
    c.paneText = text;
    c.paneTextMuted = muted;
    c.paneActiveBg = activeBg;
    c.paneActiveText = activeText;
    c.paneArrow = muted;
    return c;
}

ChromePalette SystemChromePalette() {
    return ChromePaletteForAppearance(nil);
}

}  // namespace mac
